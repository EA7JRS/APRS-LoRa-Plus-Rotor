# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Modelo de Raspberry Pi y lo que cambia de uno a otro (Zero 2 W, 3B / 3B+, 4B / 400, 5).

- GPIO del conector: gpiochip con etiqueta pinctrl-bcm2835 (Zero 2 W, 3), pinctrl-bcm2711 (4), pinctrl-rp1 (5).
- UART de los pines 8 / 10 (GPS): /dev/serial0 = mini UART con el Bluetooth activo (Zero 2 W, 3, 4); /dev/ttyAMA0 en la 5.
- Segundo UART en GPIO8 / 9 (pantalla por cable): uart4 en la 4, uart3-pi5 en la 5; la 3 y la Zero 2 W no tienen.
- Reloj: la 5 lleva el suyo (rtc0, con pila aparte); el RV-3028 del Uputronics pasa a ser el del sistema con rtc=off.
- Pulsos de los motores: la CPU marca el ritmo (Python + lgpio), mas lenta en la 3 y la Zero 2 W (limites estimados, sin medir).
"""

from __future__ import annotations

MODEL_PATH = "/proc/device-tree/model"

# familia -> pasos por segundo entre los dos motores, etiqueta del gpiochip, GPS, overlay del UART de GPIO8 / 9
FAMILIES = {
    "pi5":   {"name": "Raspberry Pi 5", "steps": 20000, "gpio": "pinctrl-rp1", "gps": "/dev/ttyAMA0",
              "link": "uart3-pi5", "rtc": True},
    "pi4":   {"name": "Raspberry Pi 4", "steps": 16000, "gpio": "pinctrl-bcm2711", "gps": "/dev/serial0",
              "link": "uart4", "rtc": False},
    "pi3":   {"name": "Raspberry Pi 3", "steps": 10000, "gpio": "pinctrl-bcm2835", "gps": "/dev/serial0",
              "link": "", "rtc": False},
    "zero2": {"name": "Raspberry Pi Zero 2 W", "steps": 10000, "gpio": "pinctrl-bcm2835", "gps": "/dev/serial0",
              "link": "", "rtc": False},
    "other": {"name": "Linux", "steps": 20000, "gpio": "pinctrl-", "gps": "/dev/serial0", "link": "", "rtc": False},
}


def model(path: str = MODEL_PATH) -> str:
    """Texto del device tree («Raspberry Pi 3 Model B Plus Rev 1.3»); «Linux» fuera de una Pi."""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read().strip("\x00\n ") or "Linux"
    except OSError:
        return "Linux"


def family(m: str) -> str:
    if "Compute Module 5" in m or "Raspberry Pi 5" in m or "Raspberry Pi 500" in m:
        return "pi5"
    if "Raspberry Pi 4" in m or "Raspberry Pi 400" in m or "Compute Module 4" in m:
        return "pi4"
    if "Zero 2" in m:
        return "zero2"
    if "Raspberry Pi 3" in m or "Compute Module 3" in m:
        return "pi3"
    return "other"


def info(m: str | None = None) -> dict:
    m = model() if m is None else m
    return {"model": m, "family": family(m), **FAMILIES[family(m)]}
