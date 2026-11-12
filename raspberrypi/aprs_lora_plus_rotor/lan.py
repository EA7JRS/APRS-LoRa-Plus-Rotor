# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Red local, como src/lan.cpp del firmware (y la conectividad del plotter y de APRS LoRa+ Display): un servidor TCP (NMEA 0183,
gpsd o Signal K), escucha NMEA por UDP (el instrumento de viento de 4") y el tiempo de OpenWeatherMap. Sirve para:

- la proteccion por viento sin la app: el mayor de viento aparente y real medidos, o el de OpenWeatherMap;
- la posicion de la estacion (baliza APRS y tiempo de internet);
- el rumbo del barco o del coche para apuntar en acimut verdadero (heading.py).

Solo biblioteca estandar. Las tramas se leen con el mismo criterio que el nucleo del plotter (lib/plotter_core/src/nmea.cpp)."""

from __future__ import annotations

import asyncio
import json
import logging
import math
import time
import urllib.parse
import urllib.request

from . import wmm

log = logging.getLogger("rotor.lan")

KN_MS = 1852.0 / 3600.0
KIND_OFF, KIND_NMEA, KIND_GPSD, KIND_SIGNALK = 0, 1, 2, 3
GPSD_WATCH = b'?WATCH={"enable":true,"nmea":true};\r\n'
SK_SUBSCRIBE = b'{"context":"vessels.self","subscribe":[{"path":"*","period":1000}]}\r\n'
FRESH_S = 10.0          # dato de la red al dia
FRESH_WEB_S = 7200.0    # dato de internet: 2 h


# ---- NMEA 0183 ----------------------------------------------------------------------------------------------------------
def nmea_fields(line: str) -> list[str] | None:
    """Campos de una sentencia con la suma bien (o sin suma); None si no vale. Quita el bloque de etiquetas \\...\\."""
    s = line.strip()
    if s.startswith("\\"):
        end = s.find("\\", 1)
        if end < 0:
            return None
        s = s[end + 1:]
    if not s or s[0] not in "$!":
        return None
    body = s[1:]
    star = body.rfind("*")
    if star >= 0:
        try:
            want = int(body[star + 1:star + 3], 16)
        except ValueError:
            return None
        body = body[:star]
        got = 0
        for ch in body:
            got ^= ord(ch)
        if got != want:
            return None
    f = body.split(",")
    return f if len(f[0]) >= 5 else None


def _num(s: str) -> float | None:
    try:
        v = float(s)
    except (TypeError, ValueError):
        return None
    return v if math.isfinite(v) else None


def _latlon(v: str, hemi: str, deg_digits: int) -> float | None:
    if len(v) < deg_digits + 2:
        return None
    d, m = _num(v[:deg_digits]), _num(v[deg_digits:])
    if d is None or m is None:
        return None
    x = d + m / 60.0
    return -x if hemi in ("S", "W") else x


class Store:
    """Ultimo valor de cada dato con su hora y su fuente ("lan" o "web")."""

    def __init__(self):
        self.v: dict[str, tuple[float, float, str]] = {}

    def set(self, key: str, value: float, src: str = "lan", now: float | None = None):
        self.v[key] = (value, time.monotonic() if now is None else now, src)

    def get(self, key: str, now: float | None = None) -> float | None:
        e = self.v.get(key)
        if not e:
            return None
        now = time.monotonic() if now is None else now
        return e[0] if now - e[1] <= (FRESH_WEB_S if e[2] == "web" else FRESH_S) else None

    def src(self, key: str) -> str:
        e = self.v.get(key)
        return e[2] if e else ""

    def age(self, key: str, now: float | None = None) -> float:
        e = self.v.get(key)
        now = time.monotonic() if now is None else now
        return now - e[1] if e else 0.0


def feed_nmea(line: str, st: Store, now: float | None = None) -> bool:
    f = nmea_fields(line)
    if not f:
        return False
    sid = f[0][-3:]                                      # GPRMC, GNRMC, IIMWV, WIMWV...
    n = len(f)
    if sid == "RMC" and n >= 7 and f[2] == "A":
        la, lo = _latlon(f[3], f[4], 2), _latlon(f[5], f[6], 3)
        if la is not None and lo is not None:
            st.set("lat", la, now=now); st.set("lon", lo, now=now)
            if n > 7 and _num(f[7]) is not None:
                st.set("sog", _num(f[7]), now=now)
            return True
    elif sid == "GGA" and n >= 7 and f[6] not in ("", "0"):
        la, lo = _latlon(f[2], f[3], 2), _latlon(f[4], f[5], 3)
        if la is not None and lo is not None:
            st.set("lat", la, now=now); st.set("lon", lo, now=now)
            return True
    elif sid == "GLL" and n >= 7 and f[6] == "A":
        la, lo = _latlon(f[1], f[2], 2), _latlon(f[3], f[4], 3)
        if la is not None and lo is not None:
            st.set("lat", la, now=now); st.set("lon", lo, now=now)
            return True
    elif sid == "HDT" and n >= 2 and _num(f[1]) is not None:
        st.set("hdg", _num(f[1]) % 360.0, now=now)
        return True
    elif sid == "VHW" and n >= 2 and _num(f[1]) is not None:
        st.set("hdg", _num(f[1]) % 360.0, now=now)
        return True
    elif sid in ("HDG", "HDM") and n >= 2 and _num(f[1]) is not None:
        st.set("hdgMag", _num(f[1]) % 360.0, now=now)
        return True
    elif sid == "MWV" and n >= 6 and f[5].startswith("A") and _num(f[3]) is not None:
        kn = _num(f[3])
        u = f[4][:1]
        if u == "M":
            kn /= KN_MS
        elif u == "K":
            kn /= 1.852
        st.set("aws" if f[2] == "R" else "tws", kn, now=now)
        return True
    return False


# ---- Signal K (deltas, unidades SI) ---------------------------------------------------------------------------------------
def feed_signalk(line: str, st: Store, now: float | None = None) -> int:
    try:
        d = json.loads(line)
    except ValueError:
        return 0
    if not isinstance(d, dict):
        return 0
    if isinstance(d.get("self"), str):                       # saludo del servidor: quien es el barco propio
        st.self_ctx = d["self"]
        return 0
    ctx = d.get("context", "vessels.self")
    if ctx != "vessels.self" and ctx != getattr(st, "self_ctx", "") and ctx != "vessels." + getattr(st, "self_ctx", ""):
        return 0                                             # otro barco
    n = 0
    for u in d.get("updates", []) or []:
        for v in (u.get("values", []) if isinstance(u, dict) else []) or []:
            if not isinstance(v, dict):
                continue
            p, x = v.get("path"), v.get("value")
            if p == "navigation.position" and isinstance(x, dict):
                la, lo = x.get("latitude"), x.get("longitude")
                if isinstance(la, (int, float)) and isinstance(lo, (int, float)):
                    st.set("lat", float(la), now=now); st.set("lon", float(lo), now=now); n += 1
            elif not isinstance(x, (int, float)):
                continue
            elif p == "navigation.headingTrue":
                st.set("hdg", math.degrees(x) % 360.0, now=now); n += 1
            elif p == "navigation.headingMagnetic":
                st.set("hdgMag", math.degrees(x) % 360.0, now=now); n += 1
            elif p == "environment.wind.speedApparent":
                st.set("aws", x / KN_MS, now=now); n += 1
            elif p in ("environment.wind.speedTrue", "environment.wind.speedOverGround"):
                st.set("tws", x / KN_MS, now=now); n += 1
            elif p in ("environment.wind.directionTrue", "environment.wind.directionGround"):
                st.set("twd", math.degrees(x) % 360.0, now=now); n += 1
    return n


def owm_url(key: str, lat: float, lon: float) -> str:
    q = urllib.parse.urlencode({"lat": f"{lat:.4f}", "lon": f"{lon:.4f}", "units": "metric", "appid": key})
    return "https://api.openweathermap.org/data/2.5/weather?" + q


def feed_owm(doc: dict, st: Store, now: float | None = None) -> bool:
    w = doc.get("wind") if isinstance(doc, dict) else None
    if not isinstance(w, dict) or not isinstance(w.get("speed"), (int, float)):
        return False
    st.set("tws", w["speed"] / KN_MS, "web", now)
    if isinstance(w.get("gust"), (int, float)):
        st.set("gust", w["gust"] / KN_MS, "web", now)
    if isinstance(w.get("deg"), (int, float)):
        st.set("twd", float(w["deg"]), "web", now)
    return True


class _Udp(asyncio.DatagramProtocol):
    def __init__(self, lan: "Lan"):
        self.lan = lan

    def datagram_received(self, data, addr):
        for line in data.decode("ascii", "replace").splitlines():
            self.lan.udp_lines += 1
            self.lan.last = time.monotonic()
            feed_nmea(line, self.lan.store)


class Lan:
    """Tareas de la red local y lo que se hace con los datos cada segundo (viento, rumbo)."""

    def __init__(self, cfg: dict, station: dict, wind, heading):
        self.cfg = cfg                  # config["lan"]
        self.station = station          # config["aprs"] (lat / lon configuradas)
        self.wind = wind                # WindGuard
        self.heading = heading          # HeadingRotor
        self.store = Store()
        self.tcp = False
        self.tcp_lines = self.udp_lines = self.sk_values = 0
        self.wx_ok = self.wx_fail = 0
        self.last = 0.0
        self.on_park = None             # al aparcar por viento: parar y aparcar (lo pone el demonio)

    @property
    def enabled(self) -> bool:
        c = self.cfg
        return bool(int(c["kind"]) or int(c["udp"]) or c["owmKey"])

    def position(self, now: float | None = None) -> tuple[float, float] | None:
        if self.cfg["pos"]:
            la, lo = self.store.get("lat", now), self.store.get("lon", now)
            if la is not None and lo is not None:
                return la, lo
        la, lo = float(self.station.get("lat", 0)), float(self.station.get("lon", 0))
        return (la, lo) if la or lo else None

    def declination(self, now: float | None = None) -> float:
        """Para un compas magnetico: la del WMM en la posicion de la estacion (lan.magAuto) o lan.magVar."""
        p = self.position(now)
        if not self.cfg.get("magAuto", True) or p is None or time.time() < 1704067200:
            return float(self.cfg["magVar"])
        return wmm.declination(p[0], p[1])

    def apply(self, now: float | None = None):
        """Cada segundo: rumbo al rotor y viento a la proteccion."""
        now = time.monotonic() if now is None else now
        h = self.store.get("hdg", now)
        if h is None and self.store.get("hdgMag", now) is not None:
            h = self.store.get("hdgMag", now) + self.declination(now)
        if h is not None:
            self.heading.set_heading(h, now)
        else:
            self.heading.tick(now)
        aws, tws, gust, twd = (self.store.get(k, now) for k in ("aws", "tws", "gust", "twd"))
        measured = aws is not None or (tws is not None and self.store.src("tws") != "web")
        self.wind.local = self.wind.anemo or (bool(self.cfg["wind"]) and measured)
        if not self.cfg["wind"] or self.wind.anemo or (aws is None and tws is None):
            return
        kn = max(x for x in (aws, tws) if x is not None)
        src = ("signalk" if int(self.cfg["kind"]) == KIND_SIGNALK else "lan") if measured else "owm"
        age = 0 if measured else int(self.store.age("tws", now))
        if self.wind.update(kn * KN_MS, gust * KN_MS if gust is not None else -1.0, int(round(twd)) if twd is not None else -1,
                            age, src, now) and self.on_park:
            self.on_park()

    def info(self) -> dict | None:
        if not self.enabled:
            return None
        c = self.cfg
        d = {"kind": int(c["kind"]), "wifi": True, "tcp": self.tcp, "lines": self.tcp_lines}
        if int(c["kind"]) == KIND_SIGNALK:
            d["sk"] = self.sk_values
        if int(c["udp"]):
            d["udp"] = self.udp_lines
        if self.last:
            d["age"] = int(time.monotonic() - self.last)
        if c["owmKey"]:
            d["wxOk"], d["wxFail"] = self.wx_ok, self.wx_fail
        la, lo = self.store.get("lat"), self.store.get("lon")
        if la is not None and lo is not None:
            d["lat"], d["lon"] = round(la, 5), round(lo, 5)
        return d

    # ---- tareas ---------------------------------------------------------------------------------------------------------
    async def run(self):
        if not self.enabled:
            return
        c = self.cfg
        log.info("red local: servidor %s %s:%s, UDP %s, OpenWeatherMap %s", c["kind"], c["host"] or "(puerta de enlace)",
                 c["port"], c["udp"], "si" if c["owmKey"] else "no")
        tasks = [asyncio.create_task(self._ticker())]
        if int(c["kind"]):
            tasks.append(asyncio.create_task(self._tcp()))
        if int(c["udp"]):
            loop = asyncio.get_running_loop()
            await loop.create_datagram_endpoint(lambda: _Udp(self), local_addr=("0.0.0.0", int(c["udp"])))
        if c["owmKey"]:
            tasks.append(asyncio.create_task(self._owm()))
        await asyncio.gather(*tasks)

    async def _ticker(self):
        while True:
            self.apply()
            await asyncio.sleep(1.0)

    async def _tcp(self):
        c = self.cfg
        kind = int(c["kind"])
        while True:
            host = c["host"] or _gateway() or "127.0.0.1"
            try:
                reader, writer = await asyncio.wait_for(asyncio.open_connection(host, int(c["port"])), 5)
                self.tcp = True
                if kind == KIND_GPSD:
                    writer.write(GPSD_WATCH)
                elif kind == KIND_SIGNALK:
                    writer.write(SK_SUBSCRIBE)
                await writer.drain()
                while line := await reader.readline():
                    if len(line) > 8192:
                        continue
                    text = line.decode("utf-8", "replace").strip()
                    if not text:
                        continue
                    self.tcp_lines += 1
                    self.last = time.monotonic()
                    if kind == KIND_SIGNALK and text.startswith("{"):
                        self.sk_values += feed_signalk(text, self.store)
                    else:
                        feed_nmea(text, self.store)
                writer.close()
            except (OSError, asyncio.TimeoutError) as e:
                log.debug("red local: %s", e)
            self.tcp = False
            await asyncio.sleep(10)

    async def _owm(self):
        while True:
            pos = self.position()
            if not pos:
                await asyncio.sleep(5)
                continue
            url = owm_url(self.cfg["owmKey"], *pos)
            try:
                doc = await asyncio.to_thread(_get_json, url)
                if feed_owm(doc, self.store):
                    self.wx_ok += 1
                    await asyncio.sleep(15 * 60)
                    continue
            except Exception as e:  # noqa: BLE001
                log.debug("OpenWeatherMap: %s", e)
            self.wx_fail += 1
            await asyncio.sleep(2 * 60)


def _get_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=8) as r:  # noqa: S310 (https fija)
        return json.loads(r.read(65536).decode("utf-8"))


def _gateway() -> str:
    """Puerta de enlace por defecto (el movil que da la WiFi), de /proc/net/route."""
    try:
        with open("/proc/net/route", encoding="ascii") as f:
            for line in f.readlines()[1:]:
                p = line.split()
                if len(p) > 2 and p[1] == "00000000":
                    g = int(p[2], 16)
                    return ".".join(str((g >> (8 * i)) & 255) for i in range(4))
    except OSError:
        pass
    return ""
