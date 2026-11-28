# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Encoder magnetico absoluto AS5600 por eje para los motores paso a paso (lazo abierto): detecta pasos perdidos (viento,
hielo, corriente baja) comparando el angulo medido con la cuenta de pasos, y opcionalmente corrige la posicion.

El AS5600 es un sensor magnetico de 12 bits (4096 cuentas/vuelta) por I2C, direccion fija 0x36. Como la direccion es fija,
para varios ejes se usa un multiplexor I2C TCA9548A (direccion 0x70-0x77): un eje = un canal del mux. La lectura del
hardware va en As5600 (smbus); la logica (comparar y decidir) va en EncoderCheck, probada sin hardware."""

from __future__ import annotations

import logging

log = logging.getLogger("encoder")

AS5600_ADDR = 0x36
RAW_ANGLE = 0x0C          # registro del angulo sin filtrar (0x0C high, 0x0D low)
STATUS = 0x0B             # bit 5 MD (iman detectado), bit 4 ML (iman debil), bit 3 MH (iman fuerte)
CPR = 4096                # cuentas por vuelta (12 bits)


def wrap180(d: float) -> float:
    """Diferencia angular en [-180, 180)."""
    return (d + 180.0) % 360.0 - 180.0


class EncoderCheck:
    """Compara el angulo medido por el encoder (cuentas crudas) con los grados contados por pasos.

    cpr: cuentas por vuelta del encoder (4096 en el AS5600). sign: sentido (-1 invierte). offset: grados del cero del
    encoder respecto al cero del eje. tol: grados a partir de los cuales se da por pasos perdidos."""

    def __init__(self, cpr: int = CPR, sign: int = 1, offset: float = 0.0, tol: float = 2.0):
        self.cpr = int(cpr)
        self.sign = 1 if sign >= 0 else -1
        self.offset = float(offset)
        self.tol = float(tol)

    def measured_deg(self, raw: int) -> float:
        """Grados absolutos (0-360) del eje segun el encoder, a partir de las cuentas crudas."""
        return (((raw % self.cpr) / self.cpr * 360.0) * self.sign - self.offset) % 360.0

    def diff(self, raw: int, counted_deg: float) -> float:
        """Grados que se ha desviado la cuenta de pasos del angulo medido (con su signo, [-180, 180))."""
        return wrap180(self.measured_deg(raw) - (counted_deg % 360.0))

    def lost(self, raw: int, counted_deg: float) -> bool:
        return abs(self.diff(raw, counted_deg)) > self.tol

    def corrected(self, raw: int, counted_deg: float) -> float:
        """Grados contados corregidos al angulo medido, conservando el numero de vueltas (solape del acimut)."""
        return counted_deg + self.diff(raw, counted_deg)


class As5600:
    """Lectura del AS5600 por I2C (smbus2), con canal de multiplexor TCA9548A opcional. Solo en la Raspberry."""

    def __init__(self, bus, addr: int = AS5600_ADDR, mux: int = -1, mux_ch: int = 0):
        self.bus, self.addr, self.mux, self.mux_ch = bus, addr, mux, mux_ch

    def _select(self):
        if self.mux >= 0:
            self.bus.write_byte(self.mux, 1 << (self.mux_ch & 7))

    def raw(self) -> int | None:
        """Cuentas crudas 0-4095, o None si no hay iman / falla el bus."""
        try:
            self._select()
            hi = self.bus.read_byte_data(self.addr, RAW_ANGLE)
            lo = self.bus.read_byte_data(self.addr, RAW_ANGLE + 1)
            return ((hi << 8) | lo) & 0x0FFF
        except Exception as e:  # noqa: BLE001
            log.warning("AS5600 0x%02x: %s", self.addr, e)
            return None

    def magnet_ok(self) -> bool | None:
        try:
            self._select()
            st = self.bus.read_byte_data(self.addr, STATUS)
            return bool(st & 0x20)                     # MD: iman detectado
        except Exception:  # noqa: BLE001
            return None


def open_bus(n: int = 1):
    """Abre /dev/i2c-n con smbus2 (o smbus si no está); solo en la Raspberry."""
    try:
        import smbus2 as sm
    except ImportError:
        import smbus as sm
    return sm.SMBus(n)
