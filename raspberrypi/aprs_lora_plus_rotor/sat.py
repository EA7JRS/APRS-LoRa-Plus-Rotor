# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Satelites: TLE (descarga y cache), lista, az/el/distancia y prediccion de pasos.

El calculo orbital va por el subpaquete vendorizado sgp4 (Brandon Rhodes, MIT; ver
sgp4/LICENSE y sgp4/ORIGEN.txt). Solo biblioteca estandar aparte de eso.

Convenio: posicion propia (lat, lon en grados, alt en metros) de gpsd, la red (lan.pos)
o la config; el satelite se propaga a km en el marco TEME, se pasa a ECEF con la hora
sideral (gstime) y se sacan azimut / elevacion / distancia en el marco local ENU."""

from __future__ import annotations

import math
import os
import time
import urllib.request

from .sgp4 import Satrec, jday
from .sgp4.propagation import gstime

DEG = 180.0 / math.pi
RAD = math.pi / 180.0
WGS84_A = 6378.137                       # km
WGS84_F = 1.0 / 298.257223563
WGS84_E2 = WGS84_F * (2.0 - WGS84_F)

DEFAULT_URL = "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"
DEFAULT_CACHE = "/var/lib/aprs-lora-plus-rotor/amateur.tle"


def _ymdhms(when: float):
    t = time.gmtime(int(when))
    return t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec + (when - int(when))


def _observer_ecef(lat: float, lon: float, alt_m: float):
    latr, lonr, h = lat * RAD, lon * RAD, alt_m / 1000.0
    sl, cl = math.sin(latr), math.cos(latr)
    n = WGS84_A / math.sqrt(1.0 - WGS84_E2 * sl * sl)
    return ((n + h) * cl * math.cos(lonr), (n + h) * cl * math.sin(lonr), (n * (1.0 - WGS84_E2) + h) * sl)


def _look(sat_ecef, lat: float, lon: float, obs_ecef):
    rx, ry, rz = sat_ecef[0] - obs_ecef[0], sat_ecef[1] - obs_ecef[1], sat_ecef[2] - obs_ecef[2]
    latr, lonr = lat * RAD, lon * RAD
    sl, cl, so, co = math.sin(latr), math.cos(latr), math.sin(lonr), math.cos(lonr)
    east = -so * rx + co * ry
    north = -sl * co * rx - sl * so * ry + cl * rz
    up = cl * co * rx + cl * so * ry + sl * rz
    rng = math.sqrt(rx * rx + ry * ry + rz * rz)
    az = math.atan2(east, north) * DEG
    if az < 0.0:
        az += 360.0
    el = math.asin(max(-1.0, min(1.0, up / rng))) * DEG
    return az, el, rng


class Sat:
    """Un satelite del TLE; el objeto SGP4 se construye la primera vez que se usa."""

    __slots__ = ("name", "l1", "l2", "_rec")

    def __init__(self, name: str, l1: str, l2: str):
        self.name = name.strip()
        self.l1, self.l2 = l1.strip(), l2.strip()
        self._rec = None

    @property
    def rec(self):
        if self._rec is None:
            self._rec = Satrec.twoline2rv(self.l1, self.l2)
        return self._rec

    @property
    def catnum(self) -> str:
        return self.l1[2:7].strip()

    def observe(self, lat: float, lon: float, alt_m: float, when: float | None = None):
        """(az, el, distancia_km) desde la posicion propia, o None si el SGP4 falla."""
        when = time.time() if when is None else when
        y, mo, d, h, mi, s = _ymdhms(when)
        jd, fr = jday(y, mo, d, h, mi, s)
        err, r, _ = self.rec.sgp4(jd, fr)
        if err:
            return None
        theta = gstime(jd + fr)
        c, s2 = math.cos(theta), math.sin(theta)
        ecef = (c * r[0] + s2 * r[1], -s2 * r[0] + c * r[1], r[2])
        return _look(ecef, lat, lon, _observer_ecef(lat, lon, alt_m))

    def next_pass(self, lat: float, lon: float, alt_m: float, from_t: float | None = None,
                  min_el: float = 0.0, horizon_h: float = 8.0, step_s: float = 60.0) -> dict | None:
        """Paso actual o siguiente: {aos, los, maxel, maxaz, up} (tiempos unix). None si no hay en el horizonte."""
        from_t = time.time() if from_t is None else from_t
        end = from_t + horizon_h * 3600.0
        t, prev_el = from_t, None
        aos = None
        up_now = False
        o = self.observe(lat, lon, alt_m, from_t)
        if o and o[1] > min_el:                       # ya esta por encima: paso en curso
            aos, up_now = from_t, True
        maxel, maxaz = -90.0, 0.0
        while t <= end:
            o = self.observe(lat, lon, alt_m, t)
            el = o[1] if o else -90.0
            if aos is None and prev_el is not None and prev_el <= min_el < el:
                aos = t - step_s * (el - min_el) / (el - prev_el) if el != prev_el else t
            if aos is not None:
                if o and el > maxel:
                    maxel, maxaz = el, o[0]
                if prev_el is not None and prev_el > min_el >= el:
                    los = t - step_s * (prev_el - min_el) / (prev_el - el) if prev_el != el else t
                    return {"aos": aos, "los": los, "maxel": maxel, "maxaz": maxaz, "up": up_now}
            prev_el = el
            t += step_s
        if aos is not None:                           # entra pero no sale dentro del horizonte
            return {"aos": aos, "los": end, "maxel": maxel, "maxaz": maxaz, "up": up_now}
        return None


def parse_tle(text: str) -> list:
    """Lista de Sat de un fichero TLE (tres lineas por satelite: nombre, 1 ..., 2 ...)."""
    out, lines = [], [ln.rstrip() for ln in text.splitlines()]
    i = 0
    while i < len(lines):
        ln = lines[i].strip()
        if ln.startswith("1 ") and i + 1 < len(lines) and lines[i + 1].strip().startswith("2 "):
            prev = lines[i - 1].strip() if i > 0 else ""
            name = prev if prev and not prev.startswith(("1 ", "2 ")) else ln[2:7]
            try:
                out.append(Sat(name, lines[i], lines[i + 1]))
            except Exception:
                pass
            i += 2
            continue
        i += 1
    return out


class SatDb:
    """TLE amateur: carga de cache, descarga (Celestrak por defecto) y lista con el proximo paso."""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.url = cfg.get("tleUrl") or DEFAULT_URL
        self.cache = cfg.get("cache") or DEFAULT_CACHE
        self.horizon_h = float(cfg.get("horizonH", 8))
        self.sats: list = []
        self.by_key: dict = {}
        self.updated = 0.0
        self._list_cache = None
        self._list_at = 0.0
        self.load()

    def load(self) -> bool:
        try:
            with open(self.cache, "r", encoding="utf-8", errors="replace") as f:
                self._set(parse_tle(f.read()), os.path.getmtime(self.cache))
            return True
        except OSError:
            return False

    def _set(self, sats: list, when: float):
        self.sats = sats
        self.by_key = {}
        for s in sats:
            self.by_key[s.name.upper()] = s
            self.by_key[s.catnum] = s
        self.updated = when
        self._list_cache = None

    def update(self, timeout: float = 20.0) -> str:
        """Descarga el TLE y lo guarda en cache. '' si va bien; si no, el motivo. Bloquea (usar en executor)."""
        try:
            req = urllib.request.Request(self.url, headers={"User-Agent": "aprs-lora-plus-rotor"})
            with urllib.request.urlopen(req, timeout=timeout) as r:
                text = r.read().decode("utf-8", "replace")
        except Exception as e:                                        # red, DNS, TLS, HTTP...
            return f"download failed: {e}"
        sats = parse_tle(text)
        if not sats:
            return "no satellites in TLE"
        try:
            os.makedirs(os.path.dirname(self.cache), exist_ok=True)
            with open(self.cache, "w", encoding="utf-8") as f:
                f.write(text)
        except OSError as e:
            return f"cache write failed: {e}"
        self._set(sats, time.time())
        return ""

    def get(self, key: str):
        if not key:
            return None
        return self.by_key.get(key.strip().upper())

    def passes(self, lat: float, lon: float, alt_m: float, now: float | None = None, cap: int = 60) -> list:
        """Lista para el selector: cada satelite con az/el actual y su proximo paso, ordenada (visibles primero)."""
        now = time.time() if now is None else now
        if self._list_cache and now - self._list_at < 30.0:
            return self._list_cache
        rows = []
        for s in self.sats[:cap]:
            o = s.observe(lat, lon, alt_m, now)
            if o is None:
                continue
            az, el, rng = o
            p = s.next_pass(lat, lon, alt_m, now, 0.0, self.horizon_h)
            row = {"name": s.name, "cat": s.catnum, "az": round(az, 1), "el": round(el, 1),
                   "rng": round(rng), "up": el > 0.0}
            if p:
                row["aos"] = int(p["aos"])
                row["maxel"] = round(p["maxel"])
                row["los"] = int(p["los"])
            rows.append(row)
        rows.sort(key=lambda r: (0 if r["up"] else 1, r.get("aos", 1e18)))
        self._list_cache, self._list_at = rows, now
        return rows
