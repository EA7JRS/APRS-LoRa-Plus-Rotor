# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Selector de antenas para los motores paso a paso de la Raspberry (mismo Waveshare Stepper Motor HAT).

Cada antena es un archivo JSON con sus datos mecanicos (ejes, reduccion 'gear', micropasos, topes, inversion,
enActive) en un bloque 'stepper' que se superpone sobre la configuracion. Asi la misma caja mueve la antena del
domo Satlink FB250+ o cualquier otra con solo elegir su archivo. De serie van en antennas/ del paquete; los del
usuario, en /etc/aprs-lora-plus-rotor/antennas/ (mismo nombre: el del usuario manda). El selector es la orden
'antenna' de la API / WebUI / app; 'antennas' devuelve la lista. docs/RASPBERRY_HATS.md.
"""

from __future__ import annotations

import json
import os

BUNDLED_DIR = os.path.join(os.path.dirname(__file__), "antennas")
USER_DIR = "/etc/aprs-lora-plus-rotor/antennas"


def _read(path: str) -> dict | None:
    try:
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
        return d if isinstance(d, dict) else None
    except (OSError, ValueError):
        return None


def _all() -> dict[str, dict]:
    """nombre -> perfil. El del usuario pisa al de serie con el mismo nombre."""
    out: dict[str, dict] = {}
    for d in (BUNDLED_DIR, USER_DIR):
        try:
            names = sorted(os.listdir(d))
        except OSError:
            continue
        for fn in names:
            if fn.endswith(".json"):
                p = _read(os.path.join(d, fn))
                if isinstance(p, dict) and isinstance(p.get("stepper"), dict):
                    out[fn[:-5]] = p
    return out


def get(name: str) -> dict | None:
    return _all().get(name)


def list_antennas() -> list[dict]:
    """Para el selector: nombre, titulo, ejes y descripcion de cada antena."""
    res = []
    for name, p in sorted(_all().items()):
        s = p["stepper"]
        axes = [a for a in ("az", "el", "x") if s.get(a, {}).get("on", a in ("az", "el"))]
        res.append({"name": name, "title": p.get("title", name), "axes": axes, "desc": p.get("desc", "")})
    return res
