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

import json
import math
import os
import re
import time
import urllib.request

from .sgp4 import Satrec, jday
from .sgp4.propagation import gstime

DEG = 180.0 / math.pi
RAD = math.pi / 180.0
WGS84_A = 6378.137                       # km
WGS84_F = 1.0 / 298.257223563
WGS84_E2 = WGS84_F * (2.0 - WGS84_F)

# La misma fuente que la app Android: fichero agregado del proyecto (rama satdata de APRS-LoRa-Plus,
# workflow satdata.yml, a diario). Formato propio: lineas "S|norad|nombre|cat|grupos", las dos de TLE
# y lineas "T|..." de transpondedores. TLE de CelesTrak, transmisores de SatNOGS DB.
DEFAULT_URL = "https://raw.githubusercontent.com/EA7JRS/APRS-LoRa-Plus/satdata/sats_all.txt"
DEFAULT_CACHE = "/var/lib/aprs-lora-plus-rotor/sats.txt"
# Satelites activos: AMSAT OSCAR Satellite Status (status.amsat.org), reportes de la gente en las ultimas 24 h.
DEFAULT_AMSAT = "https://status.amsat.org/api/v1/summary.php"
DEFAULT_AMSAT_CACHE = "/var/lib/aprs-lora-plus-rotor/amsat.json"

# Categorias (letra del grupo -> nombre), como Sat.GROUPS de la app. "l" y "g" salen de la categoria (LoRa, GEO).
# Las mayusculas I/R/E se derivan del nombre o la orbita (Inmarsat, Iridium, LEO) y no chocan con las del fichero.
GROUPS = [("s", "Estaciones"), ("a", "Amateur"), ("c", "CubeSats"), ("n", "SatNOGS"), ("w", "Meteo"),
          ("v", "Visuales"), ("x", "Ciencia"), ("d", "Educación"), ("e", "Ingeniería"), ("l", "LoRa"), ("g", "Geo"),
          ("I", "Inmarsat"), ("R", "Iridium"), ("E", "LEO")]


def _augment_groups(name: str, l2: str, groups: str) -> str:
    """Anade categorias derivadas: Inmarsat / Iridium por el nombre, LEO por el movimiento medio del TLE."""
    u = (name or "").upper()
    for key, letter in (("INMARSAT", "I"), ("IRIDIUM", "R")):
        if key in u and letter not in groups:
            groups += letter
    try:
        mm = float((l2 or "")[52:63])                         # movimiento medio (rev/dia); LEO: periodo < ~128 min
        if mm >= 11.25 and "E" not in groups:
            groups += "E"
    except ValueError:
        pass
    return groups


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
    """Un satelite del TLE; el objeto SGP4 se construye la primera vez que se usa. groups = letras de categoria."""

    __slots__ = ("name", "l1", "l2", "groups", "_rec")

    def __init__(self, name: str, l1: str, l2: str, groups: str = ""):
        self.name = name.strip()
        self.l1, self.l2 = l1.strip(), l2.strip()
        self.groups = _augment_groups(self.name, self.l2, groups)
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


def _parse_project(lines: list) -> list:
    """Formato del proyecto (y de la app): bloques "S|norad|nombre|cat|grupos", dos lineas de TLE, lineas "T|..."."""
    out, cur = [], None
    for l in lines:
        if l.startswith("S|"):
            f = l.split("|")
            cat = f[3].strip() if len(f) > 3 else ""
            grp = f[4].strip() if len(f) > 4 else ""
            for extra in (("l" if cat == "L" else ""), ("g" if cat == "G" else "")):
                if extra and extra not in grp:
                    grp += extra
            name = f[2].strip() if len(f) > 2 and f[2].strip() else (f[1].strip() if len(f) > 1 else "SAT")
            cur = [name, grp, None, None]
        elif cur is not None and cur[2] is None and l.startswith("1 "):
            cur[2] = l
        elif cur is not None and cur[3] is None and l.startswith("2 "):
            cur[3] = l
            try:
                out.append(Sat(cur[0], cur[2], cur[3], cur[1]))
            except Exception:
                pass
            cur = None
    return out


def parse_sats(text: str) -> list:
    """Lista de Sat de un fichero del proyecto (lineas S|) o de un TLE plano (nombre, 1 ..., 2 ...)."""
    lines = [ln.rstrip() for ln in text.splitlines()]
    if any(l.startswith("S|") for l in lines):
        return _parse_project(lines)
    out, i = [], 0
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


parse_tle = parse_sats                                            # compatibilidad

_NON = re.compile(r"[^A-Z0-9]")
_SPLIT = re.compile(r"[ ()\[\]/,]+")


def _desig(s: str) -> str:
    """Normaliza un designador AMSAT quitando el sufijo de modo y lo que no sea A-Z0-9: 'AO-123_[FM]' -> 'AO123'."""
    s = (s or "").upper().split("_[")[0].split(" [")[0]
    return _NON.sub("", s)


def sat_name_keys(name: str) -> set:
    """Tokens normalizados del nombre de un TLE: 'GREENCUBE (IO-117)' -> {'GREENCUBE', 'IO117'}."""
    return {_NON.sub("", t) for t in _SPLIT.split((name or "").upper()) if _NON.sub("", t)}


def amsat_active(text: str) -> set:
    """Designadores (normalizados) oidos en las ultimas 24 h segun el summary de AMSAT (todo menos 'Not Heard')."""
    try:
        d = json.loads(text)
    except Exception:
        return set()
    out = {_desig(e.get("name") or "") for e in (d.get("data") or []) if e.get("report") != "Not Heard"}
    out.discard("")
    return out


class SatDb:
    """TLE amateur: carga de cache, descarga (Celestrak por defecto) y lista con el proximo paso."""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.url = cfg.get("tleUrl") or DEFAULT_URL
        self.cache = cfg.get("cache") or DEFAULT_CACHE
        self.horizon_h = float(cfg.get("horizonH", 8))
        self.amsat_on = bool(cfg.get("amsat", True))
        self.amsat_url = cfg.get("amsatUrl") or DEFAULT_AMSAT
        self.amsat_cache = cfg.get("amsatCache") or DEFAULT_AMSAT_CACHE
        self.active_keys: set = set()                             # designadores normalizados oidos (AMSAT)
        self.active_at = 0.0
        self.sats: list = []
        self.group_letters: set = set()                           # categorias presentes en los datos
        self.by_key: dict = {}
        self.updated = 0.0
        self._list_cache = None
        self._list_key = None
        self._list_at = 0.0
        self.load()
        self.load_amsat()

    def load(self) -> bool:
        try:
            with open(self.cache, "r", encoding="utf-8", errors="replace") as f:
                self._set(parse_sats(f.read()), os.path.getmtime(self.cache))
            return True
        except OSError:
            return False

    def load_amsat(self) -> bool:
        try:
            with open(self.amsat_cache, "r", encoding="utf-8") as f:
                self.active_keys = amsat_active(f.read())
            self.active_at = os.path.getmtime(self.amsat_cache)
            return True
        except OSError:
            return False

    def _set(self, sats: list, when: float):
        self.sats = sats
        self.by_key = {}
        letters: set = set()
        for s in sats:
            self.by_key[s.name.upper()] = s
            self.by_key[s.catnum] = s
            letters.update(s.groups)
        self.group_letters = letters
        self.updated = when
        self._list_cache = None

    def groups(self) -> list:
        """Categorias que de verdad hay en los datos cargados (letra, nombre), por orden alfabetico del nombre."""
        return sorted(([g[0], g[1]] for g in GROUPS if g[0] in self.group_letters), key=lambda g: g[1].lower())

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

    def is_active(self, sat: "Sat") -> bool:
        """True si AMSAT lo ha oido en las ultimas 24 h (casa el designador con el nombre del TLE)."""
        return bool(self.active_keys) and bool(sat_name_keys(sat.name) & self.active_keys)

    def update_amsat(self, timeout: float = 15.0) -> str:
        """Descarga el estado de AMSAT y guarda que satelites estan activos. '' si va bien. Bloquea (usar en executor)."""
        if not self.amsat_on:
            return ""
        try:
            req = urllib.request.Request(self.amsat_url, headers={"User-Agent": "aprs-lora-plus-rotor"})
            with urllib.request.urlopen(req, timeout=timeout) as r:
                text = r.read().decode("utf-8", "replace")
        except Exception as e:
            return f"amsat download failed: {e}"
        keys = amsat_active(text)
        if not keys:
            return "amsat: no active satellites"
        try:
            os.makedirs(os.path.dirname(self.amsat_cache), exist_ok=True)
            with open(self.amsat_cache, "w", encoding="utf-8") as f:
                f.write(text)
        except OSError as e:
            return f"amsat cache write failed: {e}"
        self.active_keys, self.active_at, self._list_cache = keys, time.time(), None
        return ""

    def passes(self, lat: float, lon: float, alt_m: float, now: float | None = None,
               group: str | None = None, active: bool = False, cap: int = 120) -> list:
        """Lista para el selector: az/el actual, proximo paso, categoria (grp) y si esta activo (act).
        Filtra por categoria (letra de GROUPS) y/o activos (AMSAT); ordena visibles primero."""
        now = time.time() if now is None else now
        ckey = (group, active)
        if self._list_cache is not None and self._list_key == ckey and now - self._list_at < 30.0:
            return self._list_cache
        src = []
        for s in self.sats:
            if group is not None and group not in s.groups:
                continue
            if active and not self.is_active(s):
                continue
            o = s.observe(lat, lon, alt_m, now)
            if o is not None:
                src.append((s, o))
        src.sort(key=lambda t: (0 if t[1][1] > 0 else 1, -t[1][1]))      # arriba primero, mas alto primero
        limited = group is not None or active
        rows, npass = [], 0
        for s, (az, el, rng) in src:
            row = {"name": s.name, "grp": s.groups, "act": self.is_active(s),
                   "az": round(az, 1), "el": round(el, 1), "rng": round(rng), "up": el > 0.0}
            if el > 0.0 or (limited and npass < cap):                     # paso: siempre los visibles; con filtro, hasta cap mas
                p = s.next_pass(lat, lon, alt_m, now, 0.0, self.horizon_h)
                if p:
                    row["aos"], row["maxel"], row["los"] = int(p["aos"]), round(p["maxel"]), int(p["los"])
                    npass += 1
            rows.append(row)
        rows.sort(key=lambda r: (0 if r["up"] else 1, r.get("aos", 1e18), r["name"]))
        self._list_cache, self._list_key, self._list_at = rows, ckey, now
        return rows
