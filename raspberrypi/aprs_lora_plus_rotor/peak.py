# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Auto-pico con el SDR: barre una rejilla alrededor del apuntado actual, lee la potencia de la señal en cada punto (de
una sonda del receptor por la red) y lleva la antena al máximo. Sirve para afinar la puntería sobre una fuente fuerte (el
Sol, la baliza de un satélite) y, con el modelo de apuntado, para recoger referencias precisas.

La rejilla y el cálculo del máximo (centroide ponderado por potencia) van aquí como funciones puras (probadas sin
hardware); la sonda lee un número por la red (TCP: una línea con la potencia; o rigctl 'l STRENGTH')."""

from __future__ import annotations

import logging
import socket

log = logging.getLogger("peak")


def raster(span: float, step: float) -> list[tuple[float, float]]:
    """Rejilla de desplazamientos (daz, del) alrededor del centro, en bustrofedón (ida y vuelta) para no dar saltos.
    span = medio lado (grados), step = paso. Incluye el centro (0, 0)."""
    span, step = abs(float(span)), max(1e-3, abs(float(step)))
    n = int(round(span / step))
    offs = [i * step for i in range(-n, n + 1)]
    out = []
    for j, de in enumerate(offs):
        row = offs if j % 2 == 0 else offs[::-1]            # filas alternas al revés
        for da in row:
            out.append((round(da, 4), round(de, 4)))
    return out


def best(samples: list[tuple[float, float, float]], frac: float = 0.5) -> tuple[float, float, float]:
    """Mejor desplazamiento a partir de (daz, del, potencia): centroide ponderado por la potencia de los puntos por encima
    de frac entre el mínimo y el máximo (afina el pico sub-paso). Devuelve (daz, del, potencia_máxima)."""
    if not samples:
        return 0.0, 0.0, 0.0
    pmax = max(s[2] for s in samples)
    pmin = min(s[2] for s in samples)
    thr = pmin + frac * (pmax - pmin)
    top = [s for s in samples if s[2] >= thr] or [max(samples, key=lambda s: s[2])]
    w = sum(s[2] - pmin for s in top) or 1.0
    daz = sum(s[0] * (s[2] - pmin) for s in top) / w
    de = sum(s[1] * (s[2] - pmin) for s in top) / w
    return round(daz, 3), round(de, 3), pmax


class Probe:
    """Sonda de potencia por la red. kind: 'line' (abre el TCP, opcionalmente envía query, lee una línea y saca el primer
    número) o 'rigctl' (manda 'l STRENGTH' a un rigctld y lee el valor). Una medida por llamada."""

    def __init__(self, host: str, port: int, kind: str = "line", query: str = ""):
        self.host, self.port, self.kind, self.query = host, int(port), kind, query

    def read(self) -> float | None:
        try:
            with socket.create_connection((self.host, self.port), timeout=2.0) as s:
                s.settimeout(2.0)
                q = self.query or ("l STRENGTH\n" if self.kind == "rigctl" else "")
                if q:
                    s.sendall(q.encode())
                data = s.recv(256).decode("ascii", "replace")
            for tok in data.replace(",", " ").split():
                try:
                    return float(tok)
                except ValueError:
                    continue
        except Exception as e:  # noqa: BLE001
            log.warning("sonda %s:%d: %s", self.host, self.port, e)
        return None
