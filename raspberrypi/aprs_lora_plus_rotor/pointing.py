# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Modelo de apuntado multipunto: corrige el desalineado mecánico del montaje (acimut y elevación no exactos, colimación,
falta de perpendicularidad de los ejes, flexión) a partir de varias referencias de posición conocida (el Sol, la Luna, un
satélite geoestacionario Inmarsat, una estrella). Como los telescopios, cuantas más referencias, mejor el ajuste.

Modelo lineal clásico de un montaje acimut/elevación (términos en grados):
    Δaz = IA + CA / cos(el) + NP · tan(el)
    Δel = IE + TF · cos(el)
IA, IE son los índices (desfase de cada eje); CA la colimación en acimut; NP la falta de perpendicularidad az/el; TF la
flexión del tubo. ``apply`` pasa del cielo deseado al ángulo que hay que mandar al rotor; ``inverse`` hace lo contrario
(para informar la posición real). El ajuste es por mínimos cuadrados (ecuaciones normales con Gauss, sin numpy)."""

from __future__ import annotations

import math

RAD = math.pi / 180.0
# términos del modelo: nombre -> (eje 'az'/'el', función de el en grados)
_AZ_TERMS = [("ia", lambda el: 1.0), ("ca", lambda el: 1.0 / max(0.05, math.cos(el * RAD))), ("np", lambda el: math.tan(el * RAD))]
_EL_TERMS = [("ie", lambda el: 1.0), ("tf", lambda el: math.cos(el * RAD))]


def _solve(a: list[list[float]], b: list[float]) -> list[float]:
    """Resuelve a·x = b (a cuadrada) por eliminación de Gauss con pivoteo parcial. Devuelve x o ceros si es singular."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[piv][col]) < 1e-12:
            return [0.0] * n
        m[col], m[piv] = m[piv], m[col]
        for r in range(n):
            if r != col:
                f = m[r][col] / m[col][col]
                for k in range(col, n + 1):
                    m[r][k] -= f * m[col][k]
    return [m[i][n] / m[i][i] for i in range(n)]


def _fit_terms(terms, samples: list[tuple[float, float]], ridge: float = 1e-6) -> dict:
    """Mínimos cuadrados de un residuo contra una base de términos. samples: (el, residuo). Devuelve {nombre: coef}."""
    k = len(terms)
    ata = [[ridge if i == j else 0.0 for j in range(k)] for i in range(k)]
    atb = [0.0] * k
    for el, res in samples:
        row = [f(el) for _, f in terms]
        for i in range(k):
            atb[i] += row[i] * res
            for j in range(k):
                ata[i][j] += row[i] * row[j]
    x = _solve(ata, atb)
    return {terms[i][0]: x[i] for i in range(k)}


class PointingModel:
    def __init__(self, params: dict | None = None):
        self.p = {"ia": 0.0, "ca": 0.0, "np": 0.0, "ie": 0.0, "tf": 0.0}
        if params:
            self.p.update({k: float(v) for k, v in params.items() if k in self.p})

    def _daz(self, el: float) -> float:
        return sum(self.p[n] * f(el) for n, f in _AZ_TERMS)

    def _del(self, el: float) -> float:
        return sum(self.p[n] * f(el) for n, f in _EL_TERMS)

    def apply(self, az: float, el: float) -> tuple[float, float]:
        """Cielo deseado (az, el) -> ángulo a mandar al rotor para que el eje apunte ahí."""
        return (az + self._daz(el)) % 360.0, el + self._del(el)

    def inverse(self, az: float, el: float) -> tuple[float, float]:
        """Ángulo del rotor -> cielo al que apunta de verdad (resta la corrección, evaluada en el propio ángulo)."""
        return (az - self._daz(el)) % 360.0, el - self._del(el)

    @staticmethod
    def fit(points: list[tuple[float, float, float, float]], want_terms: int = 5) -> "PointingModel":
        """Ajusta el modelo a puntos (az_cielo, el_cielo, az_rotor, el_rotor). Con pocos puntos usa menos términos:
        1 punto -> solo índices (IA, IE); 2 -> + colimación / flexión; 3+ -> modelo completo."""
        n = len(points)
        if n == 0:
            return PointingModel()
        az_terms = _AZ_TERMS[:1] if n < 2 else (_AZ_TERMS[:2] if n < 3 else _AZ_TERMS)
        el_terms = _EL_TERMS[:1] if n < 2 else _EL_TERMS
        az_samples = [(se, (ar - sa + 540.0) % 360.0 - 180.0) for (sa, se, ar, er) in points]
        el_samples = [(se, er - se) for (sa, se, ar, er) in points]
        p = {}
        p.update(_fit_terms(az_terms, az_samples))
        p.update(_fit_terms(el_terms, el_samples))
        return PointingModel(p)

    def residual(self, points: list[tuple[float, float, float, float]]) -> float:
        """RMS del error del modelo sobre los puntos (grados), para saber cómo de bueno es el ajuste."""
        if not points:
            return 0.0
        s = 0.0
        for sa, se, ar, er in points:
            ma, me = self.apply(sa, se)
            s += ((ma - ar + 540.0) % 360.0 - 180.0) ** 2 + (me - er) ** 2
        return math.sqrt(s / len(points))


class PointingRotor:
    """Capa que aplica el modelo de apuntado entre la cadena y el rotor base: corrige el ángulo que se manda y
    devuelve la posición real. Va por dentro de todo (lo más cerca del rotor)."""

    def __init__(self, inner, model: PointingModel, enabled: bool = False):
        self._inner, self.model, self.enabled = inner, model, enabled

    def __getattr__(self, name):
        return getattr(self._inner, name)

    def set_pos(self, az: float, el: float) -> bool:
        if self.enabled:
            az, el = self.model.apply(az, el)
        return self._inner.set_pos(az, el)

    def set_az(self, az: float) -> bool:
        if self.enabled:
            az, _ = self.model.apply(az, self._inner.get_pos()[1])
        return self._inner.set_az(az)

    def get_pos(self) -> tuple[float, float]:
        az, el = self._inner.get_pos()
        return self.model.inverse(az, el) if self.enabled else (az, el)
