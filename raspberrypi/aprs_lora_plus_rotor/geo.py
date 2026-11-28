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
