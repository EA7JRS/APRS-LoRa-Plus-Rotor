# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Proteccion por viento, como lib/RotorCore/src/rc_wind.h del firmware: la app manda el tiempo de internet (orden "wx") y con
rachas (o viento medio) de wind.park m/s o mas el rotor se aparca y no acepta movimientos hasta que el viento baje de wind.resume
y pasen wind.hold minutos sin rachas fuertes. Sin datos nuevos sigue aparcado; "clear" lo libera a mano."""

from __future__ import annotations

import time


class WindGuard:
    def __init__(self, cfg: dict | None = None):
        self.on = False
        self.park_ms = 15.0
        self.resume_ms = 10.0
        self.hold_min = 15
        if cfg:
            self.configure(cfg)
        self.have = False
        self.locked = False
        self.wind = 0.0
        self.gust = -1.0
        self.dir = -1
        self.src = ""
        self._age_at_rx = 0
        self._rx = 0.0
        self._lock_t = 0.0
        self._strong_t = 0.0

    def configure(self, cfg: dict):
        self.on = bool(cfg.get("on", False))
        self.park_ms = float(cfg.get("park", 15.0))
        self.resume_ms = float(cfg.get("resume", 10.0))
        self.hold_min = int(cfg.get("hold", 15))

    @property
    def peak(self) -> float:
        return self.gust if self.gust >= 0 and self.gust > self.wind else self.wind

    def update(self, wind: float, gust: float, direction: int, age_s: int, src: str, now: float | None = None) -> bool:
        """Dato nuevo; True si acaba de bloquear (hay que aparcar)."""
        now = time.monotonic() if now is None else now
        self.wind, self.gust, self.dir, self._age_at_rx, self._rx, self.have = wind, gust, direction, age_s, now, True
        self.src = (src or "")[:11]
        if not self.on:
            self.locked = False
            return False
        if self.peak >= self.park_ms:
            self._strong_t = now
            if not self.locked:
                self.locked = True
                self._lock_t = now
                return True
            return False
        self._check(now)
        return False

    def tick(self, now: float | None = None):
        now = time.monotonic() if now is None else now
        if not self.on:
            self.locked = False
        else:
            self._check(now)

    def release(self):
        self.locked = False

    def age_s(self, now: float | None = None) -> int:
        now = time.monotonic() if now is None else now
        return int(self._age_at_rx + (now - self._rx))

    def hold_left_s(self, now: float | None = None) -> int:
        now = time.monotonic() if now is None else now
        left = max(self.hold_min, 0) * 60 - (now - self._strong_t)
        return int(left) if self.locked and left > 0 else 0

    def _check(self, now: float):
        if self.locked and self.peak < self.resume_ms and now - self._strong_t >= max(self.hold_min, 0) * 60:
            self.locked = False

    def info(self, now: float | None = None) -> dict | None:
        """Objeto "wind" del evento pos (como rc_api.h)."""
        if not self.have and not self.on:
            return None
        w = {"on": self.on, "park": self.park_ms, "lk": self.locked}
        if not self.have:
            return w
        w["ws"] = round(self.wind, 1)
        if self.gust >= 0:
            w["wg"] = round(self.gust, 1)
        if self.dir >= 0:
            w["wd"] = self.dir
        w["pk"] = round(self.peak, 1)
        w["age"] = self.age_s(now)
        if self.src:
            w["src"] = self.src
        if self.locked:
            w["hold"] = self.hold_left_s(now)
        return w


class WindRotor:
    """Envoltorio del rotor: rechaza movimientos mientras esta aparcado por viento; el resto pasa al rotor de verdad."""

    def __init__(self, inner, guard: WindGuard):
        self._inner = inner
        self._guard = guard

    def __getattr__(self, name):
        return getattr(self._inner, name)

    def _free(self) -> bool:
        self._guard.tick()
        return not self._guard.locked

    def set_pos(self, az: float, el: float) -> bool:
        return self._free() and self._inner.set_pos(az, el)

    def set_az(self, az: float) -> bool:
        return self._free() and self._inner.set_az(az)

    def move(self, direction: int, speed: int) -> bool:
        return self._free() and self._inner.move(direction, speed)
