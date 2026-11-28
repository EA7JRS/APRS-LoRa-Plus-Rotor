# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Sol y luna para apuntar la antena (ruido solar, rebote lunar), con las mismas formulas que lib/plotter_core/src/astro.cpp
del plotter: sol de Meeus (cap. 25, ~0,01 grados) y luna de Meeus (cap. 47, terminos principales, topocentrica con el
paralaje, ~0,03 grados). Altura sin refraccion; refraction() da la media (Saemundsson). Ademas, la fuerza Beaufort."""

from __future__ import annotations

import math

from . import celnav

_LR = [  # Meeus, tabla 47.A: D, M, M', F, longitud (1e-6 grados), distancia (m)
    (0, 0, 1, 0, 6288774, -20905355), (2, 0, -1, 0, 1274027, -3699111), (2, 0, 0, 0, 658314, -2955968),
    (0, 0, 2, 0, 213618, -569925), (0, 1, 0, 0, -185116, 48888), (0, 0, 0, 2, -114332, -3149),
    (2, 0, -2, 0, 58793, 246158), (2, -1, -1, 0, 57066, -152138), (2, 0, 1, 0, 53322, -170733),
    (2, -1, 0, 0, 45758, -204586), (0, 1, -1, 0, -40923, -129620), (1, 0, 0, 0, -34720, 108743),
    (0, 1, 1, 0, -30383, 104755), (2, 0, 0, -2, 15327, 10321), (0, 0, 1, 2, -12528, 0),
    (0, 0, 1, -2, 10980, 79661), (4, 0, -1, 0, 10675, -34782), (0, 0, 3, 0, 10034, -23210),
    (4, 0, -2, 0, 8548, -21636), (2, 1, -1, 0, -7888, 24208), (2, 1, 0, 0, -6766, 30824),
    (1, 0, -1, 0, -5163, -8379), (1, 1, 0, 0, 4987, -16675), (2, -1, 1, 0, 4036, -12831),
    (2, 0, 2, 0, 3994, -10445), (4, 0, 0, 0, 3861, -11650), (2, 0, -3, 0, 3665, 14403),
    (0, 1, -2, 0, -2689, -7003), (2, 0, -1, 2, -2602, 0), (2, -1, -2, 0, 2390, 10056),
    (1, 0, 1, 0, -2348, 6322), (2, -2, 0, 0, 2236, -9884),
]
_B = [  # tabla 47.B: latitud (1e-6 grados)
    (0, 0, 0, 1, 5128122), (0, 0, 1, 1, 280602), (0, 0, 1, -1, 277693), (2, 0, 0, -1, 173237),
    (2, 0, -1, 1, 55413), (2, 0, -1, -1, 46271), (2, 0, 0, 1, 32573), (0, 0, 2, 1, 17198),
    (2, 0, 1, -1, 9266), (0, 0, 2, -1, 8822), (2, -1, 0, -1, 8216), (2, 0, -2, -1, 4324),
    (2, 0, 1, 1, 4200), (2, 1, 0, -1, -3359), (2, -1, -1, 1, 2463), (2, -1, 0, 1, 2211),
    (2, -1, -1, -1, 2065), (0, 1, -1, -1, -1870), (4, 0, -1, -1, 1828), (0, 1, 0, 1, -1794),
]

_rad, _deg = math.radians, math.degrees


def julian_day(t: float) -> float:
    """Dia juliano de un instante Unix (UTC)."""
    return t / 86400.0 + 2440587.5


def _gmst(jd: float) -> float:
    tu = (jd - 2451545.0) / 36525.0
    return (280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * tu * tu) % 360.0


# Matriz ICRS -> galactica (Hipparcos / IAU 1958, epoca J2000). Su traspuesta pasa de galactica a ecuatorial.
_EQ2GAL = ((-0.0548755604, -0.8734370902, -0.4838350155),
           (0.4941094279, -0.4448296300, 0.7469822445),
           (-0.8676661490, -0.1980763734, 0.4559837762))


def radec_to_azel(ra: float, dec: float, lat: float, lon: float, t: float) -> tuple[float, float]:
    """(azimut, altura) en grados de un punto fijo del cielo (RA, Dec J2000 en grados) desde (lat, lon) en el instante t
    (Unix UTC). Para radioastronomia: seguir una fuente con el giro sidereo de la Tierra (linea de hidrogeno 1420 MHz)."""
    lst = _gmst(julian_day(t)) + lon                          # tiempo sidereo local (grados)
    ha = _rad(lst - ra)                                       # angulo horario
    az, alt = _altaz(lat, ha, _rad(dec))
    return az, alt


def galactic_to_radec(l: float, b: float) -> tuple[float, float]:
    """Coordenadas galacticas (l, b en grados) -> ecuatoriales (RA, Dec J2000 en grados)."""
    lr, br = _rad(l), _rad(b)
    vg = (math.cos(br) * math.cos(lr), math.cos(br) * math.sin(lr), math.sin(br))
    ve = tuple(sum(_EQ2GAL[j][i] * vg[j] for j in range(3)) for i in range(3))     # traspuesta: galactica -> ecuatorial
    ra = _deg(math.atan2(ve[1], ve[0])) % 360.0
    dec = _deg(math.asin(max(-1.0, min(1.0, ve[2]))))
    return ra, dec


def _altaz(lat: float, ha: float, dec: float) -> tuple[float, float]:
    la = _rad(lat)
    alt = _deg(math.asin(math.sin(la) * math.sin(dec) + math.cos(la) * math.cos(dec) * math.cos(ha)))
    az = _deg(math.atan2(-math.sin(ha) * math.cos(dec), math.sin(dec) * math.cos(la) - math.cos(dec) * math.sin(la) * math.cos(ha)))
    return az % 360.0, alt


def sun_position(t: float, lat: float, lon: float) -> tuple[float, float]:
    """(azimut, altura) del sol en grados. Lugar aparente con precision de almanaque (celnav, como el plotter)."""
    gha, dec = celnav.sun_gha_dec(julian_day(t))
    alt, az = celnav.body_alt_az(gha, dec, lat, lon)
    return az, alt


def moon_position(t: float, lat: float, lon: float) -> tuple[float, float, float]:
    """(azimut, altura, distancia en km) de la luna, topocentricos."""
    jd = julian_day(t) + 69.0 / 86400.0
    T = (jd - 2451545.0) / 36525.0
    Lp = (218.3164477 + 481267.88123421 * T) % 360
    D = (297.8501921 + 445267.1114034 * T) % 360
    M = (357.5291092 + 35999.0502909 * T) % 360
    Mp = (134.9633964 + 477198.8675055 * T) % 360
    F = (93.2720950 + 483202.0175233 * T) % 360
    E = 1 - 0.002516 * T - 0.0000074 * T * T
    A1, A2, A3 = (119.75 + 131.849 * T) % 360, (53.09 + 479264.290 * T) % 360, (313.45 + 481266.484 * T) % 360
    sl = sr = sb = 0.0
    for d, m, mp, f, l, r in _LR:
        arg = _rad(d * D + m * M + mp * Mp + f * F)
        e = 1.0 if m == 0 else (E if abs(m) == 1 else E * E)
        sl += e * l * math.sin(arg)
        sr += e * r * math.cos(arg)
    for d, m, mp, f, b in _B:
        e = 1.0 if m == 0 else (E if abs(m) == 1 else E * E)
        sb += e * b * math.sin(_rad(d * D + m * M + mp * Mp + f * F))
    sl += 3958 * math.sin(_rad(A1)) + 1962 * math.sin(_rad(Lp - F)) + 318 * math.sin(_rad(A2))
    sb += (-2235 * math.sin(_rad(Lp)) + 382 * math.sin(_rad(A3)) + 175 * math.sin(_rad(A1 - F)) + 175 * math.sin(_rad(A1 + F))
           + 127 * math.sin(_rad(Lp - Mp)) - 115 * math.sin(_rad(Lp + Mp)))
    Om, Ls = (125.04452 - 1934.136261 * T) % 360, (280.4665 + 36000.7698 * T) % 360
    dpsi = (-17.20 * math.sin(_rad(Om)) - 1.32 * math.sin(_rad(2 * Ls)) - 0.23 * math.sin(_rad(2 * Lp))
            + 0.21 * math.sin(_rad(2 * Om)))
    deps = (9.20 * math.cos(_rad(Om)) + 0.57 * math.cos(_rad(2 * Ls)) + 0.10 * math.cos(_rad(2 * Lp))
            - 0.09 * math.cos(_rad(2 * Om)))
    lam, bet = _rad(Lp + sl / 1e6 + dpsi / 3600.0), _rad(sb / 1e6)
    dist = 385000.56 + sr / 1000.0
    eps = _rad(23.4392911 - 0.0130042 * T + deps / 3600.0)
    ra = math.atan2(math.sin(lam) * math.cos(eps) - math.tan(bet) * math.sin(eps), math.cos(lam))
    dec = math.asin(math.sin(bet) * math.cos(eps) + math.cos(bet) * math.sin(eps) * math.sin(lam))
    jdu = jd - 69.0 / 86400.0
    H = _rad(_gmst(jdu) + dpsi / 3600.0 * math.cos(eps) + lon) - ra
    la = _rad(lat)
    sin_pi = 6378.14 / dist
    u = math.atan(0.99664719 * math.tan(la))
    rs, rc = 0.99664719 * math.sin(u), math.cos(u)
    dra = math.atan2(-rc * sin_pi * math.sin(H), math.cos(dec) - rc * sin_pi * math.cos(H))
    dec2 = math.atan2((math.sin(dec) - rs * sin_pi) * math.cos(dra), math.cos(dec) - rc * sin_pi * math.cos(H))
    az, alt = _altaz(lat, H - dra, dec2)
    return az, alt, dist


def refraction(alt: float) -> float:
    """Refraccion media (grados) para una altura verdadera: altura aparente = alt + refraction(alt)."""
    h = max(alt, -1.9)
    return 1.02 / math.tan(_rad(h + 10.3 / (h + 5.11))) / 60.0


_BF = (0, 3, 6, 10, 16, 21, 27, 33, 40, 47, 55, 63)   # limites superiores en nudos (escala de la OMM)


def beaufort(knots: float) -> int:
    """Fuerza Beaufort 0..12 con el viento redondeado al nudo (como plotter::beaufort)."""
    k = math.floor(max(knots, 0.0) + 0.5)
    for f, top in enumerate(_BF):
        if k <= top:
            return f
    return 12
