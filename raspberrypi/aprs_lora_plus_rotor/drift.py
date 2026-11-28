# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Barrido por deriva (drift scan) para radioastronomía (línea H1, 1420 MHz): la antena se queda **fija** apuntando al
meridiano local a una declinación, y el giro de la Tierra hace cruzar el cielo por el haz (la ascensión recta aumenta).
Tras un tiempo de permanencia, salta a la siguiente declinación y repite. Así se barre una franja del cielo sin motores
en movimiento durante la medida (lo mide el receptor SDR; aquí solo se apunta y se registra).

En el paso por el meridiano (culminación superior), un astro de declinación ``dec`` está en elevación 90 − |lat − dec|, al
**sur** (az 180) si dec ≤ lat o al **norte** (az 0) si dec > lat. La geometría va aquí como funciones puras (probadas sin
hardware); la orquestación (apuntar, esperar, registrar) la hace el demonio."""

from __future__ import annotations


def meridian_azel(lat: float, dec: float) -> tuple[float, float]:
    """Acimut y elevación del paso por el meridiano local de la declinación dec (culminación superior). La elevación puede
    salir negativa (no llega a culminar sobre el horizonte); el que llama la descarta."""
    el = 90.0 - abs(lat - dec)
    az = 180.0 if dec <= lat else 0.0
    return az, el


def plan(lat: float, dec_start: float, dec_end: float, step: float) -> list[tuple[float, float, float]]:
    """Lista de (dec, az, el) del barrido entre dec_start y dec_end con paso ``step``, saltando las que no culminan sobre
    el horizonte. El paso va de dec_start a dec_end en el sentido que haga falta."""
    step = abs(float(step)) or 1.0
    sgn = 1.0 if dec_end >= dec_start else -1.0
    n = int(round(abs(dec_end - dec_start) / step))
    out = []
    for i in range(n + 1):
        dec = dec_start + sgn * i * step
        az, el = meridian_azel(lat, dec)
        if el >= 0.0:
            out.append((round(dec, 3), az, round(el, 3)))
    return out
