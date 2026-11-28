# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Geodesia para el enlace punto a punto (P2P): azimut y elevacion de una estacion hacia otra, y locator Maidenhead.

El azimut es el rumbo verdadero de circulo maximo (el que usa el rotor). La elevacion tiene en cuenta la diferencia de
altura y, opcional, la curvatura terrestre (en enlaces largos sale ~0 o ligeramente negativa). Solo biblioteca estandar.
"""

from __future__ import annotations

import math

R_EARTH = 6371000.0        # radio medio de la Tierra (m)


def great_circle(lat1: float, lon1: float, lat2: float, lon2: float) -> tuple[float, float]:
    """Rumbo verdadero inicial (grados 0-360) y distancia (m) de (lat1,lon1) a (lat2,lon2)."""
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dl = math.radians(lon2 - lon1)
    y = math.sin(dl) * math.cos(p2)
    x = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    brg = (math.degrees(math.atan2(y, x)) + 360.0) % 360.0
    a = math.sin((p2 - p1) / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    dist = 2 * R_EARTH * math.asin(min(1.0, math.sqrt(a)))
    return brg, dist


def azel(lat1: float, lon1: float, alt1: float, lat2: float, lon2: float, alt2: float,
         curvature: bool = True) -> tuple[float, float, float]:
    """Azimut, elevacion (grados) y distancia (m) de la estacion 1 a la 2. alt en m sobre el nivel del mar."""
    az, d = great_circle(lat1, lon1, lat2, lon2)
    dh = float(alt2) - float(alt1)
    drop = (d * d) / (2 * R_EARTH) if curvature else 0.0     # caida por curvatura terrestre
    el = math.degrees(math.atan2(dh - drop, d)) if d > 0 else (90.0 if dh > 0 else 0.0)
    return az, el, d


R_EQ_KM = 6378.137          # radio ecuatorial (km), para satelites
GEO_ALT_KM = 35786.0        # altura de la orbita geoestacionaria sobre la superficie (km)


def _ecef(lat: float, lon: float, r_km: float) -> tuple[float, float, float]:
    """Punto ECEF (km) en la latitud/longitud dadas, a radio r_km del centro de la Tierra (esfera)."""
    la, lo = math.radians(lat), math.radians(lon)
    return (r_km * math.cos(la) * math.cos(lo), r_km * math.cos(la) * math.sin(lo), r_km * math.sin(la))


def azel_to_point(lat: float, lon: float, obs_r_km: float, target_km: tuple[float, float, float]) -> tuple[float, float, float]:
    """Azimut (0-360), elevacion (grados) y distancia (km) a un punto dado en ECEF (km), desde el observador."""
    la, lo = math.radians(lat), math.radians(lon)
    o = _ecef(lat, lon, obs_r_km)
    v = (target_km[0] - o[0], target_km[1] - o[1], target_km[2] - o[2])
    up = (math.cos(la) * math.cos(lo), math.cos(la) * math.sin(lo), math.sin(la))
    east = (-math.sin(lo), math.cos(lo), 0.0)
    north = (-math.sin(la) * math.cos(lo), -math.sin(la) * math.sin(lo), math.cos(la))
    e = v[0] * east[0] + v[1] * east[1] + v[2] * east[2]
    n = v[0] * north[0] + v[1] * north[1] + v[2] * north[2]
    u = v[0] * up[0] + v[1] * up[1] + v[2] * up[2]
    dist = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
    az = (math.degrees(math.atan2(e, n)) + 360.0) % 360.0
    el = math.degrees(math.atan2(u, math.hypot(e, n)))
    return az, el, dist


def geostationary_azel(lat: float, lon: float, sat_lon: float, obs_alt_m: float = 0.0) -> tuple[float, float, float]:
    """Az, el (grados) y distancia (km) a un satelite geoestacionario en la longitud sat_lon, desde (lat, lon)."""
    sat = _ecef(0.0, sat_lon, R_EQ_KM + GEO_ALT_KM)
    return azel_to_point(lat, lon, R_EQ_KM + obs_alt_m / 1000.0, sat)


# ---- Maidenhead ---------------------------------------------------------------------------------------------------------

def locator_to_latlon(loc: str) -> tuple[float, float] | None:
    """Centro del cuadro de un locator Maidenhead (4, 6 u 8 caracteres). None si no es valido."""
    s = (loc or "").strip().upper()
    if len(s) not in (4, 6, 8) or not s[0:2].isalpha() or not s[2:4].isdigit():
        return None
    try:
        lon = (ord(s[0]) - 65) * 20.0 - 180.0 + (ord(s[2]) - 48) * 2.0
        lat = (ord(s[1]) - 65) * 10.0 - 90.0 + (ord(s[3]) - 48) * 1.0
        size_lon, size_lat = 2.0, 1.0
        if len(s) >= 6:
            if not s[4:6].isalpha():
                return None
            lon += (ord(s[4]) - 65) * (2.0 / 24)
            lat += (ord(s[5]) - 65) * (1.0 / 24)
            size_lon, size_lat = 2.0 / 24, 1.0 / 24
        if len(s) == 8:
            if not s[6:8].isdigit():
                return None
            lon += (ord(s[6]) - 48) * (size_lon / 10)
            lat += (ord(s[7]) - 48) * (size_lat / 10)
            size_lon, size_lat = size_lon / 10, size_lat / 10
    except (ValueError, IndexError):
        return None
    return lat + size_lat / 2, lon + size_lon / 2


def latlon_to_locator(lat: float, lon: float, length: int = 6) -> str:
    """Locator Maidenhead (4, 6 u 8 caracteres) de una posicion."""
    lon = (float(lon) + 180.0) % 360.0
    lat = float(lat) + 90.0
    f1, lon = divmod(lon, 20.0); g1, lat = divmod(lat, 10.0)
    f2, lon = divmod(lon, 2.0);  g2, lat = divmod(lat, 1.0)
    out = chr(65 + int(f1)) + chr(65 + int(g1)) + str(int(f2)) + str(int(g2))
    if length >= 6:
        f3, lon = divmod(lon, 2.0 / 24); g3, lat = divmod(lat, 1.0 / 24)
        out += chr(65 + int(f3)) + chr(65 + int(g3))
    if length >= 8:
        f4 = lon / (2.0 / 240); g4 = lat / (1.0 / 240)
        out += str(int(f4)) + str(int(g4))
    return out
