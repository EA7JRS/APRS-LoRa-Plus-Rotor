# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Rotor en un barco o en un coche, como lib/RotorCore/src/rc_heading.h del firmware: el acimut que se pide y el que se da son
verdaderos; el rotor gira respecto a la proa. Se resta el rumbo al pedir, se suma al leer y se vuelve a apuntar cuando el rumbo
cambia step grados o mas (como mucho una vez por segundo). Apagado o sin rumbo, todo pasa tal cual; sin rumbo nuevo sigue con el
ultimo. Va por fuera de WindRotor."""

from __future__ import annotations

import time


def norm360(a: float) -> float:
    return a % 360.0


def ang_diff(a: float, b: float) -> float:
    d = norm360(a - b)
    return 360.0 - d if d > 180 else d


class HeadingRotor:
    def __init__(self, inner, enabled: bool = False, step: float = 3.0, stale_s: float = 10.0):
        self._inner = inner
        self.enabled = enabled
        self.step = step
        self.stale_s = stale_s
        self.have = False
        self.heading = 0.0
        self._hdg_t = 0.0
        self._aim_hdg = 0.0
        self._aim_t = 0.0
        self.target: tuple[float, float | None] | None = None   # (az verdadero, el o None)

    def __getattr__(self, name):
        return getattr(self._inner, name)

    @property
    def active(self) -> bool:
        return self.enabled and self.have

    @property
    def min_az(self) -> float:
        return 0.0 if self.active else self._inner.min_az

    @property
    def max_az(self) -> float:
        return 360.0 if self.active else self._inner.max_az

    def set_heading(self, deg: float, now: float | None = None):
        now = time.monotonic() if now is None else now
        self.heading, self._hdg_t, self.have = norm360(deg), now, True
        self.tick(now)

    def tick(self, now: float | None = None):
        now = time.monotonic() if now is None else now
        if self.active and self.target and now - self._aim_t >= 1.0 and ang_diff(self.heading, self._aim_hdg) >= self.step:
            self._aim(now)

    def stale(self, now: float | None = None) -> bool:
        now = time.monotonic() if now is None else now
        return self.have and now - self._hdg_t > self.stale_s

    def age_s(self, now: float | None = None) -> int:
        now = time.monotonic() if now is None else now
        return int(now - self._hdg_t) if self.have else 0

    def relative(self, true_az: float) -> float:
        """Acimut del rotor para un acimut verdadero: de las vueltas posibles, la mas cercana a la posicion actual."""
        r = norm360(true_az - self.heading)
        cur = self._inner.get_pos()[0]
        lo, hi = self._inner.min_az, self._inner.max_az
        cands = [r + 360.0 * k for k in (-1, 0, 1) if lo - 1e-6 <= r + 360.0 * k <= hi + 1e-6]
        return min(cands, key=lambda c: abs(c - cur)) if cands else r

    def _aim(self, now: float) -> bool:
        az, el = self.target
        self._aim_hdg, self._aim_t = self.heading, now
        rel = self.relative(az)
        ok = self._inner.set_pos(rel, el) if el is not None else self._inner.set_az(rel)
        if not ok:
            self.target = None
        return ok

    # ---- interfaz del rotor ---------------------------------------------------------------------------------------------
    def set_pos(self, az: float, el: float) -> bool:
        if not self.active:
            return self._inner.set_pos(az, el)
        self.target = (az, el)
        return self._aim(self._hdg_t)

    def set_az(self, az: float) -> bool:
        if not self.active:
            return self._inner.set_az(az)
        self.target = (az, None)
        return self._aim(self._hdg_t)

    def get_pos(self) -> tuple[float, float]:
        az, el = self._inner.get_pos()
        return (norm360(az + self.heading), el) if self.active else (az, el)

    def stop(self) -> bool:
        self.target = None
        return self._inner.stop()

    def stop_az(self) -> bool:
        self.target = None
        return self._inner.stop_az()

    def park(self) -> bool:
        self.target = None
        return self._inner.park()

    def move(self, direction: int, speed: int) -> bool:
        self.target = None
        return self._inner.move(direction, speed)

    def info_event(self, enabled: bool, now: float | None = None) -> dict | None:
        """Objeto "hdg" del evento pos (como src/lan.cpp)."""
        if not enabled and not self.have:
            return None
        d = {"on": enabled}
        if self.have:
            d["v"] = round(self.heading, 1)
            d["age"] = self.age_s(now)
            if self.stale(now):
                d["old"] = True
        return d
