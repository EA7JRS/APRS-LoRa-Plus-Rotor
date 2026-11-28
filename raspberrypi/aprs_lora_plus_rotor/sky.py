# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Seguir el sol o la luna, como lib/RotorCore/src/rc_sky.h del firmware. Va por fuera de todo:
clientes -> SkyRotor -> HeadingRotor -> WindRotor -> rotctld. Una orden de un cliente (pos, stop, park, move) deja de seguir.
Cada period segundos calcula la posicion (astro.py) y, si se ha movido step grados o mas, apunta; por debajo de min_el espera."""

from __future__ import annotations

import time

from . import astro, geo
from .heading import ang_diff

OFF, SUN, MOON, P2P, SAT = 0, 1, 2, 3, 4
NAMES = {OFF: "off", SUN: "sun", MOON: "moon", P2P: "p2p", SAT: "sat"}


def parse(s) -> int:
    return {"off": OFF, "sun": SUN, "moon": MOON, "p2p": P2P, "sat": SAT}.get(s, -1) if isinstance(s, str) else -1


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
    def __init__(self, inner, step: float = 1.0, min_el: float = 0.0, refraction: bool = True, period: float = 1.0,
                 curvature: bool = True):
        self._inner = inner
        self.step, self.min_el, self.refraction, self.period = step, min_el, refraction, period
        self.target = OFF
        self.lat = self.lon = self.alt = 0.0
        self.az = self.el = 0.0
        self.below = False
        self.aims = 0
        self.curv = curvature                                 # P2P: tener en cuenta la curvatura terrestre en la elevacion
        self.peer: tuple[float, float, float] | None = None   # (lat, lon, alt) de la otra estacion
        self.peer_src = ""                                    # fixed / net / aprs
        self.dist = 0.0                                       # m a la otra estacion (P2P) o al satelite (SAT)
        self.sat = None                                       # satelite elegido (sat.Sat) para target == SAT
        self.sat_pass: dict | None = None                     # paso en curso o siguiente {aos, los, maxel, maxaz}
        self.horizon_h = 8.0                                  # horizonte de la prediccion de pasos
        self._pass_at = -1e9
        self._aimed: tuple[float, float] | None = None
        self._last = -1e9

    def __getattr__(self, name):
        return getattr(self._inner, name)

    @property
    def tracking(self) -> bool:
        return self.target != OFF

    def start(self, target: int, pos: tuple[float, float] | None, t: float | None = None, now: float | None = None,
              alt: float = 0.0) -> str:
        """'' si empieza (o para con OFF); si no, el motivo."""
        if target == OFF:
            self.stop_tracking()
            return ""
        t = time.time() if t is None else t
        if pos is None:
            return "no station position (aprs.lat / lon or lan.pos)"
        if target in (SUN, MOON, SAT) and t < 1704067200:
            return "no time (GPS or NTP)"
        if target == P2P and self.peer is None:
            return "no peer position (P2P: locator / coords, app or APRS)"
        if target == SAT and self.sat is None:
            return "no satellite selected"
        self.target, (self.lat, self.lon), self.alt = target, pos, float(alt)
        self.sat_pass, self._pass_at = None, -1e9
        self._aimed, self._last = None, -1e9
        self.tick(t, now)
        return ""

    def set_peer(self, lat: float, lon: float, alt: float = 0.0, src: str = ""):
        """Posicion de la otra estacion (P2P), en vivo desde la app o APRS, o fija de la config."""
        self.peer = (float(lat), float(lon), float(alt))
        if src:
            self.peer_src = src
        if self.target == P2P:
            self._aimed, self._last = None, -1e9              # re-apunta ya a la nueva posicion

    def set_sat(self, sat):
        """Satelite elegido (sat.Sat); si ya se estaba siguiendo SAT, re-apunta al nuevo."""
        self.sat = sat
        if self.target == SAT:
            self._aimed, self._last, self.sat_pass, self._pass_at = None, -1e9, None, -1e9

    def stop_tracking(self):
        self.target, self.below = OFF, False

    SAT_PERIOD = 0.1                                          # los satelites LEO se mueven rapido: recalcular y re-apuntar a 10 Hz

    def tick(self, t: float | None = None, now: float | None = None):
        now = time.monotonic() if now is None else now
        per = self.SAT_PERIOD if self.target == SAT else self.period
        if self.target == OFF or now - self._last < per:
            return
        self._last = now
        if self.target == P2P:
            if self.peer is None:
                return
            az, el, self.dist = geo.azel(self.lat, self.lon, self.alt, self.peer[0], self.peer[1], self.peer[2], self.curv)
        elif self.target == SAT:
            if self.sat is None:
                return
            tt = time.time() if t is None else t
            o = self.sat.observe(self.lat, self.lon, self.alt, tt)
            if o is None:
                self.below = True
                return
            az, el, rng = o
            self.dist = rng * 1000.0
            if now - self._pass_at > 30.0:                     # refresca la prediccion del paso cada 30 s
                self.sat_pass = self.sat.next_pass(self.lat, self.lon, self.alt, tt, self.min_el, self.horizon_h)
                self._pass_at = now
        else:
            az, el = position(self.target, self.lat, self.lon, time.time() if t is None else t, self.refraction)
        self.az, self.el = az, el
        self.below = self.target != P2P and el < self.min_el   # P2P siempre apunta (enlace fijo al horizonte)
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
        d = {"t": NAMES[self.target], "az": round(self.az, 2), "el": round(self.el, 2), "below": self.below}
        if self.target == P2P:
            d["km"] = round(self.dist / 1000.0, 1)
        elif self.target == SAT:
            d["km"] = round(self.dist / 1000.0)
            if self.sat is not None:
                d["name"] = self.sat.name
            if self.sat_pass:
                d["maxel"] = round(self.sat_pass["maxel"])
                d["los"] = time.strftime("%H:%M", time.gmtime(self.sat_pass["los"]))   # UTC
        return d

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
