# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Navegacion astronomica con precision de almanaque (0,1'), con las mismas formulas y valores de prueba que
lib/plotter_core/src/celnav.cpp del plotter: AHG y declinacion del Sol (VSOP87 recortada, nutacion, aberracion), AHG de
Aries y AHS y declinacion de las 57 estrellas de navegacion y la Polar (movimiento propio, precesion, nutacion y
aberracion; catalogo de PyEphem / Hipparcos). GENERADO desde el nucleo del plotter (tablas); no editar las tablas a mano."""

from __future__ import annotations

import math

_AS = math.pi / (180.0 * 3600.0)
_DELTA_T = 69.2

# nombre, magnitud, AR J2000 (h), dec J2000 (grados), movimiento propio en AR (x cos dec) y en dec (mas/anio)
STARS = [
    ("Acamar", 2.88, 2.97102074, -40.30467239, -53.53, 25.71),
    ("Achernar", 0.45, 1.62856849, -57.23675744, 88.02, -40.08),
    ("Acrux", 0.77, 12.44330439, -63.09909168, -35.37, -14.73),
    ("Adhara", 1.50, 6.97709679, -28.97208374, 2.63, 2.29),
    ("Aldebaran", 0.87, 4.59867740, 16.50930138, 62.78, -189.36),
    ("Alioth", 1.76, 12.90048595, 55.95982123, 111.74, -8.99),
    ("Alkaid", 1.85, 13.79234379, 49.31326512, -121.23, -15.56),
    ("Alnair", 1.73, 22.13721819, -46.96097539, 127.60, -147.91),
    ("Alnilam", 1.69, 5.60355929, -1.20191983, 1.49, -1.06),
    ("Alphard", 1.99, 9.45978980, -8.65860253, -14.49, 33.25),
    ("Alphecca", 2.22, 15.57813004, 26.71469307, 120.38, -89.44),
    ("Alpheratz", 2.07, 0.13979405, 29.09043197, 135.68, -162.95),
    ("Altair", 0.76, 19.84638864, 8.86832203, 536.82, 385.54),
    ("Ankaa", 2.40, 0.43806972, -42.30598144, 232.76, -353.64),
    ("Antares", 1.06, 16.49012803, -26.43200250, -10.16, -23.21),
    ("Arcturus", -0.05, 14.26102001, 19.18241038, -1093.45, -1999.40),
    ("Atria", 1.91, 16.81108191, -69.02771505, 17.85, -32.92),
    ("Avior", 1.86, 8.37523211, -59.50948307, -25.34, 22.72),
    ("Bellatrix", 1.64, 5.41885085, 6.34970223, -8.75, -13.28),
    ("Betelgeuse", 0.45, 5.91952924, 7.40706274, 27.33, 10.86),
    ("Canopus", -0.62, 6.39919718, -52.69566045, 19.99, 23.67),
    ("Capella", 0.08, 5.27815528, 45.99799106, 75.52, -427.13),
    ("Deneb", 1.25, 20.69053187, 45.28033800, 1.56, 1.55),
    ("Denebola", 2.14, 11.81766043, 14.57206038, -499.02, -113.78),
    ("Diphda", 2.04, 0.72649196, -17.98660457, 232.79, 32.71),
    ("Dubhe", 1.81, 11.06213019, 61.75103324, -136.46, -35.25),
    ("Elnath", 1.65, 5.43819816, 28.60745000, 23.28, -174.22),
    ("Eltanin", 2.24, 17.94343608, 51.48889500, -8.52, -23.05),
    ("Enif", 2.38, 21.73643281, 9.87501126, 30.02, 1.38),
    ("Fomalhaut", 1.17, 22.96084626, -29.62223601, 329.22, -164.22),
    ("Gacrux", 1.59, 12.51943314, -57.11321175, 27.94, -264.33),
    ("Gienah", 2.58, 12.26343617, -17.54192948, -159.58, 22.31),
    ("Hadar", 0.61, 14.06372347, -60.37303932, -33.96, -25.06),
    ("Hamal", 2.01, 2.11955753, 23.46242310, 190.73, -145.77),
    ("Kaus Australis", 1.79, 18.40286620, -34.38461611, -39.61, -124.05),
    ("Kochab", 2.07, 14.84509068, 74.15550496, -32.29, 11.91),
    ("Markab", 2.49, 23.07934827, 15.20526441, 61.10, -42.56),
    ("Menkar", 2.54, 3.03799227, 4.08973396, -11.81, -78.76),
    ("Menkent", 2.06, 14.11137457, -36.36995451, -519.29, -517.87),
    ("Miaplacidus", 1.67, 9.21999318, -69.71720776, -157.66, 108.91),
    ("Mirfak", 1.79, 3.40538065, 49.86117958, 24.11, -26.01),
    ("Nunki", 2.05, 18.92109048, -26.29672225, 13.87, -52.65),
    ("Peacock", 1.94, 20.42746051, -56.73509009, 7.71, -86.15),
    ("Pollux", 1.16, 7.75526397, 28.02619865, -625.69, -45.95),
    ("Procyon", 0.40, 7.65503283, 5.22499314, -716.57, -1034.58),
    ("Rasalhague", 2.08, 17.58224183, 12.56003481, 110.08, -222.61),
    ("Regulus", 1.36, 10.13953074, 11.96720709, -249.40, 4.91),
    ("Rigel", 0.18, 5.24229787, -8.20164055, 1.87, -0.56),
    ("Rigil Kentaurus", -0.01, 14.66013779, -60.83397588, -3678.19, 481.84),
    ("Sabik", 2.43, 17.17296871, -15.72491023, 41.16, 97.65),
    ("Schedar", 2.24, 0.67512237, 56.53733107, 50.36, -32.17),
    ("Shaula", 1.62, 17.56014444, -37.10382115, -8.90, -29.95),
    ("Sirius", -1.44, 6.75247697, -16.71611569, -546.01, -1223.08),
    ("Spica", 0.98, 13.41988313, -11.16132203, -42.50, -31.73),
    ("Suhail", 2.23, 9.13326624, -43.43258935, -23.21, 14.28),
    ("Vega", 0.03, 18.61564903, 38.78369185, 201.02, 287.46),
    ("Zubenelgenubi", 2.75, 14.84797587, -16.04177819, -105.69, -69.00),
    ("Polaris", 1.97, 2.53030100, 89.26410949, 44.22, -11.74),
]

_L0 = [(175347046, 0, 0), (3341656, 4.6692568, 6283.07585), (34894, 4.6261, 12566.1517), (3497, 2.7441, 5753.3849), (3418, 2.8289, 3.5231), (3136, 3.6277, 77713.7715), (2676, 4.4181, 7860.4194), (2343, 6.1352, 3930.2097), (1324, 0.7425, 11506.7698), (1273, 2.0371, 529.691), (1199, 1.1096, 1577.3435), (990, 5.233, 5884.927), (902, 2.045, 26.298), (857, 3.508, 398.149), (780, 1.179, 5223.694), (753, 2.533, 5507.553), (505, 4.583, 18849.228), (492, 4.205, 775.523), (357, 2.92, 0.067), (317, 5.849, 11790.629), (284, 1.899, 796.298), (271, 0.315, 10977.079), (243, 0.345, 5486.778), (206, 4.806, 2544.314), (205, 1.869, 5573.143), (202, 2.458, 6069.777), (156, 0.833, 213.299), (132, 3.411, 2942.463), (126, 1.083, 20.775), (115, 0.645, 0.98), (103, 0.636, 4694.003), (102, 0.976, 15720.839), (102, 4.267, 7.114), (99, 6.21, 2146.17), (98, 0.68, 155.42), (86, 5.98, 161000.69), (85, 1.3, 6275.96), (85, 3.67, 71430.7), (80, 1.81, 17260.15)]
_L1 = [(628331966747.0, 0, 0), (206059, 2.678235, 6283.07585), (4303, 2.6351, 12566.1517), (425, 1.59, 3.523), (119, 5.796, 26.298), (109, 2.966, 1577.344), (93, 2.59, 18849.23), (72, 1.14, 529.69), (68, 1.87, 398.15), (67, 4.41, 5507.55), (59, 2.89, 5223.69), (56, 2.17, 155.42), (45, 0.4, 796.3), (36, 0.47, 775.52), (29, 2.65, 7.11), (21, 5.34, 0.98), (19, 1.85, 5486.78), (19, 4.97, 213.3), (17, 2.99, 6275.96), (16, 0.03, 2544.31)]
_L2 = [(52919, 0, 0), (8720, 1.0721, 6283.0758), (309, 0.867, 12566.152), (27, 0.05, 3.52), (16, 5.19, 26.3), (16, 3.68, 155.42), (10, 0.76, 18849.23), (9, 2.06, 77713.77), (7, 0.83, 775.52), (5, 4.66, 1577.34)]
_L3 = [(289, 5.844, 6283.076), (35, 0, 0), (17, 5.49, 12566.15)]


def _series(t, tau):
    return sum(a * math.cos(b + c * tau) for a, b, c in t)


def _nutation(T):
    om = math.radians(125.04452 - 1934.136261 * T)
    L = math.radians(280.4665 + 36000.7698 * T)
    Lm = math.radians(218.3165 + 481267.8813 * T)
    dpsi = (-17.20 * math.sin(om) - 1.32 * math.sin(2 * L) - 0.23 * math.sin(2 * Lm) + 0.21 * math.sin(2 * om)) * _AS
    deps = (9.20 * math.cos(om) + 0.57 * math.cos(2 * L) + 0.10 * math.cos(2 * Lm) - 0.09 * math.cos(2 * om)) * _AS
    eps = (84381.448 - 46.8150 * T - 0.00059 * T * T + 0.001813 * T ** 3) * _AS + deps
    return dpsi, deps, eps


def _tt(jd):
    return (jd + _DELTA_T / 86400.0 - 2451545.0) / 36525.0


def _sun_geometric(T):
    tau = T / 10.0
    L = (_series(_L0, tau) + tau * (_series(_L1, tau) + tau * (_series(_L2, tau) + tau * (_series(_L3, tau) + tau * -114.0)))) * 1e-8
    lon = (L + math.pi) % (2 * math.pi) - 0.09033 * _AS
    M = math.radians(357.52911 + 35999.05029 * T)
    e = 0.016708634 - 0.000042037 * T
    C = math.radians((1.914602 - 0.004817 * T) * math.sin(M) + 0.019993 * math.sin(2 * M) + 0.000289 * math.sin(3 * M))
    return lon, 1.000001018 * (1 - e * e) / (1 + e * math.cos(M + C))


def _gast(jd):
    d = jd - 2451545.0
    tu = d / 36525.0
    gmst = 280.46061837 + 360.98564736629 * d + 0.000387933 * tu * tu - tu ** 3 / 38710000.0
    dpsi, _, eps = _nutation(_tt(jd))
    return (math.radians(gmst % 360.0) + dpsi * math.cos(eps)) % (2 * math.pi)


def aries_gha(jd: float) -> float:
    """AHG de Aries en grados (jd: dia juliano UTC)."""
    return math.degrees(_gast(jd))


def sun_gha_dec(jd: float) -> tuple[float, float]:
    """(AHG, declinacion) del Sol en grados."""
    T = _tt(jd)
    lon, R = _sun_geometric(T)
    dpsi, _, eps = _nutation(T)
    lam = lon + dpsi - 20.4898 * _AS / R
    ra = math.atan2(math.cos(eps) * math.sin(lam), math.cos(lam))
    dec = math.asin(math.sin(eps) * math.sin(lam))
    return math.degrees((_gast(jd) - ra) % (2 * math.pi)), math.degrees(dec)


def sun_semidiameter_arcmin(jd: float) -> float:
    return 959.63 / 60.0 / _sun_geometric(_tt(jd))[1]


def find_star(name: str):
    for s in STARS:
        if s[0].lower() == name.lower():
            return s
    return None


def star_sha_dec(star, jd: float) -> tuple[float, float]:
    """(AHS, declinacion) aparentes de la fecha, en grados."""
    _, _, ra_h, dec_deg, pm_ra, pm_dec = star
    T = _tt(jd)
    years = T * 100.0
    d0 = math.radians(dec_deg + pm_dec / 3.6e6 * years)
    a0 = math.radians(ra_h * 15.0) + math.radians(pm_ra / 3.6e6 * years) / math.cos(math.radians(dec_deg))
    zeta = (2306.2181 * T + 0.30188 * T * T + 0.017998 * T ** 3) * _AS
    z = (2306.2181 * T + 1.09468 * T * T + 0.018203 * T ** 3) * _AS
    th = (2004.3109 * T - 0.42665 * T * T - 0.041833 * T ** 3) * _AS
    A = math.cos(d0) * math.sin(a0 + zeta)
    B = math.cos(th) * math.cos(d0) * math.cos(a0 + zeta) - math.sin(th) * math.sin(d0)
    C = math.sin(th) * math.cos(d0) * math.cos(a0 + zeta) + math.cos(th) * math.sin(d0)
    a = math.atan2(A, B) + z
    d = math.asin(C)
    dpsi, deps, eps = _nutation(T)
    sun, _ = _sun_geometric(T)
    e = 0.016708634 - 0.000042037 * T
    peri = math.radians(102.93735 + 1.71946 * T)
    k = 20.49552 * _AS
    ca, sa, cd, sd, ce, te = math.cos(a), math.sin(a), math.cos(d), math.sin(d), math.cos(eps), math.tan(eps)
    da = ((ce + math.sin(eps) * sa * sd / cd) * dpsi - ca * sd / cd * deps
          - k * (ca * math.cos(sun) * ce + sa * math.sin(sun)) / cd + e * k * (ca * math.cos(peri) * ce + sa * math.sin(peri)) / cd)
    dd = (math.sin(eps) * ca * dpsi + sa * deps
          - k * (math.cos(sun) * ce * (te * cd - sa * sd) + ca * sd * math.sin(sun))
          + e * k * (math.cos(peri) * ce * (te * cd - sa * sd) + ca * sd * math.sin(peri)))
    return math.degrees((-(a + da)) % (2 * math.pi)), math.degrees(d + dd)


def body_alt_az(gha: float, dec: float, lat: float, lon: float) -> tuple[float, float]:
    """(altura, azimut) verdaderos de un astro; lon + al este."""
    ha, de, la = math.radians(gha + lon), math.radians(dec), math.radians(lat)
    alt = math.degrees(math.asin(math.sin(la) * math.sin(de) + math.cos(la) * math.cos(de) * math.cos(ha)))
    az = math.degrees(math.atan2(-math.sin(ha) * math.cos(de), math.sin(de) * math.cos(la) - math.cos(de) * math.sin(la) * math.cos(ha)))
    return alt, az % 360.0
