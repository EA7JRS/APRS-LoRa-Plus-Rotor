# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Seguir el sol o la luna, como lib/RotorCore/src/rc_sky.h del firmware. Va por fuera de todo:
clientes -> SkyRotor -> HeadingRotor -> WindRotor -> rotctld. Una orden de un cliente (pos, stop, park, move) deja de seguir.
Cada period segundos calcula la posicion (astro.py) y, si se ha movido step grados o mas, apunta; por debajo de min_el espera."""

from __future__ import annotations

import time

from . import astro
from .heading import ang_diff

OFF, SUN, MOON = 0, 1, 2
NAMES = {OFF: "off", SUN: "sun", MOON: "moon"}


def parse(s) -> int:
    return {"off": OFF, "sun": SUN, "moon": MOON}.get(s, -1) if isinstance(s, str) else -1


def position(target: int, lat: float, lon: float, t: float, refraction: bool = True) -> tuple[float, float] | None:
    if target == SUN:
        az, el = astro.sun_position(t, lat, lon)
    elif target == MOON:
        az, el, _ = astro.moon_position(t, lat, lon)
    else:
        return None
    if refraction:
        el += astro.refraction(el)
    return az, el


class SkyRotor:
    def __init__(self, inner, step: float = 1.0, min_el: float = 0.0, refraction: bool = True, period: float = 2.0):
        self._inner = inner
        self.step, self.min_el, self.refraction, self.period = step, min_el, refraction, period
        self.target = OFF
        self.lat = self.lon = 0.0
        self.az = self.el = 0.0
        self.below = False
        self.aims = 0
        self._aimed: tuple[float, float] | None = None
        self._last = -1e9

    def __getattr__(self, name):
        return getattr(self._inner, name)

    @property
    def tracking(self) -> bool:
        return self.target != OFF

    def start(self, target: int, pos: tuple[float, float] | None, t: float | None = None, now: float | None = None) -> str:
        """'' si empieza (o para con OFF); si no, el motivo."""
        if target == OFF:
            self.stop_tracking()
            return ""
        t = time.time() if t is None else t
        if pos is None:
            return "no station position (aprs.lat / lon or lan.pos)"
        if t < 1704067200:
            return "no time (GPS or NTP)"
        self.target, (self.lat, self.lon) = target, pos
        self._aimed, self._last = None, -1e9
        self.tick(t, now)
        return ""

    def stop_tracking(self):
        self.target, self.below = OFF, False

    def tick(self, t: float | None = None, now: float | None = None):
        now = time.monotonic() if now is None else now
        if self.target == OFF or now - self._last < self.period:
            return
        self._last = now
        az, el = position(self.target, self.lat, self.lon, time.time() if t is None else t, self.refraction)
        self.az, self.el = az, el
        self.below = el < self.min_el
        if self.below:
            return
        if self._aimed and ang_diff(az, self._aimed[0]) < self.step and abs(el - self._aimed[1]) < self.step:
            return
        inner = self._inner
        if inner.has_el:
            ok = inner.set_pos(az, min(max(el, inner.min_el), inner.max_el))
        else:
            ok = inner.set_az(az)
        if ok is not False:
            self._aimed = (az, el)
            self.aims += 1

    def info_event(self) -> dict | None:
        """Objeto "trk" del evento pos (como rc_api.h)."""
        if not self.tracking:
            return None
        return {"t": NAMES[self.target], "az": round(self.az, 1), "el": round(self.el, 1), "below": self.below}

    # Ordenes de los clientes: dejan de seguir
    def set_pos(self, az, el):
        self.stop_tracking()
        return self._inner.set_pos(az, el)

    def set_az(self, az):
        self.stop_tracking()
        return self._inner.set_az(az)

    def stop(self):
        self.stop_tracking()
        return self._inner.stop()

    def stop_az(self):
        self.stop_tracking()
        return self._inner.stop_az()

    def park(self):
        self.stop_tracking()
        return self._inner.park()

    def move(self, *a, **k):
        self.stop_tracking()
        return self._inner.move(*a, **k)
