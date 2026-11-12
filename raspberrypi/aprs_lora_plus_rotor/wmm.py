# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Modelo Magnetico Mundial (WMM2025 de NOAA NCEI / British Geological Survey, coeficientes de dominio publico, valido
de 2025.0 a 2030.0): declinacion para un compas que solo da rumbo magnetico. Mismo algoritmo que lib/plotter_core/src/wmm.cpp
del plotter (sintesis armonica esferica de grado 12); load_cof() acepta el WMM.COF nuevo cuando caduque."""

from __future__ import annotations

import math

EPOCH = 2025.0
NAME = "WMM-2025"
# (n, m, g, h, g', h') en nT y nT/anio
COEFFS = [
    (1, 0, -29351.8, 0.0, 12.0, 0.0),
    (1, 1, -1410.8, 4545.4, 9.7, -21.5),
    (2, 0, -2556.6, 0.0, -11.6, 0.0),
    (2, 1, 2951.1, -3133.6, -5.2, -27.7),
    (2, 2, 1649.3, -815.1, -8.0, -12.1),
    (3, 0, 1361.0, 0.0, -1.3, 0.0),
    (3, 1, -2404.1, -56.6, -4.2, 4.0),
    (3, 2, 1243.8, 237.5, 0.4, -0.3),
    (3, 3, 453.6, -549.5, -15.6, -4.1),
    (4, 0, 895.0, 0.0, -1.6, 0.0),
    (4, 1, 799.5, 278.6, -2.4, -1.1),
    (4, 2, 55.7, -133.9, -6.0, 4.1),
    (4, 3, -281.1, 212.0, 5.6, 1.6),
    (4, 4, 12.1, -375.6, -7.0, -4.4),
    (5, 0, -233.2, 0.0, 0.6, 0.0),
    (5, 1, 368.9, 45.4, 1.4, -0.5),
    (5, 2, 187.2, 220.2, 0.0, 2.2),
    (5, 3, -138.7, -122.9, 0.6, 0.4),
    (5, 4, -142.0, 43.0, 2.2, 1.7),
    (5, 5, 20.9, 106.1, 0.9, 1.9),
    (6, 0, 64.4, 0.0, -0.2, 0.0),
    (6, 1, 63.8, -18.4, -0.4, 0.3),
    (6, 2, 76.9, 16.8, 0.9, -1.6),
    (6, 3, -115.7, 48.8, 1.2, -0.4),
    (6, 4, -40.9, -59.8, -0.9, 0.9),
    (6, 5, 14.9, 10.9, 0.3, 0.7),
    (6, 6, -60.7, 72.7, 0.9, 0.9),
    (7, 0, 79.5, 0.0, -0.0, 0.0),
    (7, 1, -77.0, -48.9, -0.1, 0.6),
    (7, 2, -8.8, -14.4, -0.1, 0.5),
    (7, 3, 59.3, -1.0, 0.5, -0.8),
    (7, 4, 15.8, 23.4, -0.1, 0.0),
    (7, 5, 2.5, -7.4, -0.8, -1.0),
    (7, 6, -11.1, -25.1, -0.8, 0.6),
    (7, 7, 14.2, -2.3, 0.8, -0.2),
    (8, 0, 23.2, 0.0, -0.1, 0.0),
    (8, 1, 10.8, 7.1, 0.2, -0.2),
    (8, 2, -17.5, -12.6, 0.0, 0.5),
    (8, 3, 2.0, 11.4, 0.5, -0.4),
    (8, 4, -21.7, -9.7, -0.1, 0.4),
    (8, 5, 16.9, 12.7, 0.3, -0.5),
    (8, 6, 15.0, 0.7, 0.2, -0.6),
    (8, 7, -16.8, -5.2, -0.0, 0.3),
    (8, 8, 0.9, 3.9, 0.2, 0.2),
    (9, 0, 4.6, 0.0, -0.0, 0.0),
    (9, 1, 7.8, -24.8, -0.1, -0.3),
    (9, 2, 3.0, 12.2, 0.1, 0.3),
    (9, 3, -0.2, 8.3, 0.3, -0.3),
    (9, 4, -2.5, -3.3, -0.3, 0.3),
    (9, 5, -13.1, -5.2, 0.0, 0.2),
    (9, 6, 2.4, 7.2, 0.3, -0.1),
    (9, 7, 8.6, -0.6, -0.1, -0.2),
    (9, 8, -8.7, 0.8, 0.1, 0.4),
    (9, 9, -12.9, 10.0, -0.1, 0.1),
    (10, 0, -1.3, 0.0, 0.1, 0.0),
    (10, 1, -6.4, 3.3, 0.0, 0.0),
    (10, 2, 0.2, 0.0, 0.1, -0.0),
    (10, 3, 2.0, 2.4, 0.1, -0.2),
    (10, 4, -1.0, 5.3, -0.0, 0.1),
    (10, 5, -0.6, -9.1, -0.3, -0.1),
    (10, 6, -0.9, 0.4, 0.0, 0.1),
    (10, 7, 1.5, -4.2, -0.1, 0.0),
    (10, 8, 0.9, -3.8, -0.1, -0.1),
    (10, 9, -2.7, 0.9, -0.0, 0.2),
    (10, 10, -3.9, -9.1, -0.0, -0.0),
    (11, 0, 2.9, 0.0, 0.0, 0.0),
    (11, 1, -1.5, 0.0, -0.0, -0.0),
    (11, 2, -2.5, 2.9, 0.0, 0.1),
    (11, 3, 2.4, -0.6, 0.0, -0.0),
    (11, 4, -0.6, 0.2, 0.0, 0.1),
    (11, 5, -0.1, 0.5, -0.1, -0.0),
    (11, 6, -0.6, -0.3, 0.0, -0.0),
    (11, 7, -0.1, -1.2, -0.0, 0.1),
    (11, 8, 1.1, -1.7, -0.1, -0.0),
    (11, 9, -1.0, -2.9, -0.1, 0.0),
    (11, 10, -0.2, -1.8, -0.1, 0.0),
    (11, 11, 2.6, -2.3, -0.1, 0.0),
    (12, 0, -2.0, 0.0, 0.0, 0.0),
    (12, 1, -0.2, -1.3, 0.0, -0.0),
    (12, 2, 0.3, 0.7, -0.0, 0.0),
    (12, 3, 1.2, 1.0, -0.0, -0.1),
    (12, 4, -1.3, -1.4, -0.0, 0.1),
    (12, 5, 0.6, -0.0, -0.0, -0.0),
    (12, 6, 0.6, 0.6, 0.1, -0.0),
    (12, 7, 0.5, -0.1, -0.0, -0.0),
    (12, 8, -0.1, 0.8, 0.0, 0.0),
    (12, 9, -0.4, 0.1, 0.0, -0.0),
    (12, 10, -0.2, -1.0, -0.1, -0.0),
    (12, 11, -1.3, 0.1, -0.0, 0.0),
    (12, 12, -0.7, 0.2, -0.1, -0.1),
]

_A, _F, _REF = 6378.137, 1 / 298.257223563, 6371.2
_N = 12


class Wmm:
    def __init__(self, coeffs=COEFFS, epoch: float = EPOCH, name: str = NAME):
        self.epoch, self.name = epoch, name
        self._c = {(n, m): (g, h, gd, hd) for n, m, g, h, gd, hd in coeffs}

    @classmethod
    def load_cof(cls, text: str) -> "Wmm":
        lines = [l for l in text.splitlines() if l.strip()]
        head = lines[0].split()
        epoch, name = float(head[0]), head[1]
        rows = []
        for l in lines[1:]:
            if l.startswith("9999"):
                break
            p = l.split()
            rows.append((int(p[0]), int(p[1]), *map(float, p[2:6])))
        if len(rows) != 90:
            raise ValueError("WMM.COF: hacen falta 90 coeficientes")
        return cls(rows, epoch, name)

    @property
    def valid_until(self) -> float:
        return self.epoch + 5

    def field(self, lat: float, lon: float, alt_km: float, year: float) -> dict:
        """D (declinacion, + este), I (inclinacion), H, F, X, Y, Z (nT)."""
        if not -90 <= lat <= 90:
            raise ValueError("lat")
        e2 = _F * (2 - _F)
        sl, cl = math.sin(math.radians(lat)), math.cos(math.radians(lat))
        rc = _A / math.sqrt(1 - e2 * sl * sl)
        p, z = (rc + alt_km) * cl, (rc * (1 - e2) + alt_km) * sl
        r = math.hypot(p, z)
        phic = math.asin(z / r)
        x, c = math.sin(phic), math.cos(phic)
        dt = year - self.epoch
        P = [[0.0] * (_N + 1) for _ in range(_N + 1)]
        dP = [[0.0] * (_N + 1) for _ in range(_N + 1)]
        P[0][0], P[1][0], P[1][1] = 1.0, x, c
        for n in range(2, _N + 1):
            P[n][n] = c * math.sqrt((2 * n - 1) / (2 * n)) * P[n - 1][n - 1]
        for m in range(_N + 1):
            for n in range(max(m + 1, 2), _N + 1):
                b = math.sqrt((n - 1) ** 2 - m * m) if n - 1 >= m else 0.0
                P[n][m] = ((2 * n - 1) * x * P[n - 1][m] - (b * P[n - 2][m] if n - 2 >= m else 0.0)) / math.sqrt(n * n - m * m)
        s = max(c, 1e-12)
        for n in range(1, _N + 1):
            for m in range(n + 1):
                prev = math.sqrt(n * n - m * m) * P[n - 1][m] if n - 1 >= m else 0.0
                dP[n][m] = (n * x * P[n][m] - prev) / s
        X = Y = Z = 0.0
        ar = _REF / r
        arn = ar * ar
        lr = math.radians(lon)
        for n in range(1, _N + 1):
            arn *= ar
            xs = ys = zs = 0.0
            for m in range(n + 1):
                g0, h0, gd, hd = self._c[(n, m)]
                g, h = g0 + dt * gd, h0 + dt * hd
                cm, sm = math.cos(m * lr), math.sin(m * lr)
                t1 = g * cm + h * sm
                xs += t1 * dP[n][m]
                ys += m * (g * sm - h * cm) * P[n][m]
                zs += t1 * P[n][m]
            X += arn * xs
            Y += arn * ys
            Z -= (n + 1) * arn * zs
        Y /= s
        psi = phic - math.radians(lat)
        Xg, Zg = X * math.cos(psi) - Z * math.sin(psi), X * math.sin(psi) + Z * math.cos(psi)
        H = math.hypot(Xg, Y)
        return {"D": math.degrees(math.atan2(Y, Xg)), "I": math.degrees(math.atan2(Zg, H)), "H": H,
                "F": math.hypot(H, Zg), "X": Xg, "Y": Y, "Z": Zg}

    def declination(self, lat: float, lon: float, year: float, alt_km: float = 0.0) -> float:
        return self.field(lat, lon, alt_km, year)["D"]


DEFAULT = Wmm()


def decimal_year(t: float) -> float:
    """Anio decimal de un instante Unix (UTC)."""
    import datetime
    d = datetime.datetime.fromtimestamp(t, datetime.timezone.utc)
    start = datetime.datetime(d.year, 1, 1, tzinfo=datetime.timezone.utc)
    end = datetime.datetime(d.year + 1, 1, 1, tzinfo=datetime.timezone.utc)
    return d.year + (d - start).total_seconds() / (end - start).total_seconds()


def declination(lat: float, lon: float, t: float | None = None) -> float:
    import time
    return DEFAULT.declination(lat, lon, decimal_year(time.time() if t is None else t))
