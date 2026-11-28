# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Compensacion de actitud para el rotor en una plataforma movil (coche, barco) o cuando el mastil se gira con el viento.

Va en la cadena por dentro del rumbo: clientes -> SkyRotor -> HeadingRotor (guinada / rumbo) -> AttitudeRotor (escora,
cabeceo, giro del mastil) -> WindRotor -> rotor. Dada la actitud de la base (roll = escora, pitch = cabeceo, yaw = giro
del mastil medido por el giroscopo), transforma el az/el del objetivo (nivelado, referido a la proa) al marco inclinado
del rotor, para que el eje de la antena apunte al objetivo. Vale para 2 y 3 ejes (el 3.er eje estabiliza mecanicamente;
esto afina lo que no llegue). Los signos de cada eje son configurables para calibrar con la IMU real."""

from __future__ import annotations

import math
import time

from .heading import ang_diff, norm360

RAD = math.pi / 180.0
DEG = 180.0 / math.pi


def _rz(v, a):                                                     # giro alrededor de Z (arriba)
    c, s = math.cos(a), math.sin(a)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2])


def _rx(v, a):                                                     # giro alrededor de X (estribor)
    c, s = math.cos(a), math.sin(a)
    return (v[0], v[1] * c - v[2] * s, v[1] * s + v[2] * c)


def _ry(v, a):                                                     # giro alrededor de Y (proa)
    c, s = math.cos(a), math.sin(a)
    return (v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c)


def _vec(az: float, el: float):
    ce = math.cos(el * RAD)
    return (ce * math.sin(az * RAD), ce * math.cos(az * RAD), math.sin(el * RAD))   # x estribor, y proa, z arriba


def _azel(v):
    az = math.atan2(v[0], v[1]) * DEG
    if az < 0:
        az += 360.0
    return az, math.asin(max(-1.0, min(1.0, v[2]))) * DEG


def aim(az: float, el: float, roll: float, pitch: float, yaw: float) -> tuple[float, float]:
    """Objetivo nivelado (az referido a la proa, el sobre el horizonte) -> az/el que debe mandar el rotor en la base
    inclinada. Con roll = pitch = yaw = 0 devuelve lo mismo (sin cambios)."""
    v = _vec(az, el)
    v = _rz(v, -yaw * RAD)
    v = _rx(v, -pitch * RAD)
    v = _ry(v, -roll * RAD)
    return _azel(v)


def unaim(az_m: float, el_m: float, roll: float, pitch: float, yaw: float) -> tuple[float, float]:
    """Inverso de aim: del az/el del rotor (base inclinada) al az/el nivelado (para informar la posicion real)."""
    v = _vec(az_m, el_m)
    v = _ry(v, roll * RAD)
    v = _rx(v, pitch * RAD)
    v = _rz(v, yaw * RAD)
    return _azel(v)


class AttitudeRotor:
    def __init__(self, inner, enabled: bool = False, step: float = 0.5, stale_s: float = 5.0,
                 roll_sign: int = 1, pitch_sign: int = 1, yaw_sign: int = 1,
                 roll_zero: float = 0.0, pitch_zero: float = 0.0, yaw_zero: float = 0.0):
        self._inner = inner
        self.enabled = enabled
        self.step = step
        self.stale_s = stale_s
        self.rs, self.ps, self.ys = roll_sign, pitch_sign, yaw_sign
        self.zr, self.zp, self.zy = float(roll_zero), float(pitch_zero), float(yaw_zero)   # cero del montaje (tara)
        self.have = False
        self.roll = self.pitch = self.yaw = 0.0                   # efectivos (crudo con signo menos la tara)
        self._raw = (0.0, 0.0, 0.0)                               # crudo con signo, antes de la tara
        self._att_t = 0.0
        self._aim_att = (0.0, 0.0, 0.0)
        self._aim_t = 0.0
        self.target: tuple[float, float | None] | None = None

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

    def set_attitude(self, roll: float, pitch: float, yaw: float = 0.0, now: float | None = None):
        now = time.monotonic() if now is None else now
        self._raw = (self.rs * float(roll), self.ps * float(pitch), self.ys * float(yaw))
        self._apply()
        self._att_t, self.have = now, True
        self.tick(now)

    def _apply(self):
        self.roll, self.pitch, self.yaw = self._raw[0] - self.zr, self._raw[1] - self.zp, self._raw[2] - self.zy

    def tare(self):
        """Pone a cero la escora, el cabeceo y el giro: la actitud cruda actual pasa a ser la referencia (0)."""
        self.zr, self.zp, self.zy = self._raw
        self._apply()
        if self.target:
            self._aim(time.monotonic())
        return (round(self.zr, 2), round(self.zp, 2), round(self.zy, 2))

    def set_zero(self, roll_zero: float, pitch_zero: float, yaw_zero: float = 0.0):
        self.zr, self.zp, self.zy = float(roll_zero), float(pitch_zero), float(yaw_zero)
        self._apply()
        if self.target:
            self._aim(time.monotonic())

    def _att(self):
        return (self.roll, self.pitch, self.yaw)

    def tick(self, now: float | None = None):
        now = time.monotonic() if now is None else now
        if not (self.active and self.target and now - self._aim_t >= 0.5):
            return
        a, b = self._att(), self._aim_att
        if ang_diff(a[0], b[0]) >= self.step or ang_diff(a[1], b[1]) >= self.step or ang_diff(a[2], b[2]) >= self.step:
            self._aim(now)

    def stale(self, now: float | None = None) -> bool:
        now = time.monotonic() if now is None else now
        return self.have and now - self._att_t > self.stale_s

    def age_s(self, now: float | None = None) -> int:
        now = time.monotonic() if now is None else now
        return int(now - self._att_t) if self.have else 0

    def _aim(self, now: float) -> bool:
        az, el = self.target
        self._aim_att, self._aim_t = self._att(), now
        if el is None:
            am, _ = aim(az, 0.0, self.roll, self.pitch, self.yaw)
            return self._inner.set_az(am)
        am, em = aim(az, el, self.roll, self.pitch, self.yaw)
        em = min(max(em, self._inner.min_el), self._inner.max_el)
        return self._inner.set_pos(am, em)

    # ---- interfaz del rotor ---------------------------------------------------------------------------------------------
    def set_pos(self, az: float, el: float) -> bool:
        if not self.active:
            return self._inner.set_pos(az, el)
        self.target = (az, el)
        return self._aim(time.monotonic())

    def set_az(self, az: float) -> bool:
        if not self.active:
            return self._inner.set_az(az)
        self.target = (az, None)
        return self._aim(time.monotonic())

    def get_pos(self) -> tuple[float, float]:
        az, el = self._inner.get_pos()
        if not self.active:
            return az, el
        return unaim(az, el, self.roll, self.pitch, self.yaw)

    def info_event(self, now: float | None = None) -> dict | None:
        """Objeto "att" del evento pos."""
        if not self.enabled and not self.have:
            return None
        d = {"on": self.enabled}
        if self.have:
            d["r"], d["p"], d["y"] = round(self.roll, 1), round(self.pitch, 1), round(self.yaw, 1)
            d["age"] = self.age_s(now)
            if self.stale(now):
                d["old"] = True
        return d

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
