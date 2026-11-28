# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""TMC2209 por UART (un hilo) para el 3.er eje: fija la corriente, los micropasos y StallGuard sin tocar jumpers, y
permite el homing sensorless (sin final de carrera): cuando el eje topa, el driver detecta el bloqueo (StallGuard) y pone
su salida DIAG a 1; esa patilla se usa como el final de carrera del eje (ver stepper/RASPBERRY_HATS.md).

El protocolo UART del TMC2209 (datasheet, apartado "UART interface") usa datagramas con un CRC de 8 bits (polinomio 0x07,
LSB primero). La codificacion y el CRC van aqui como funciones puras (probadas sin hardware); la clase Tmc2209 escribe por
un puerto serie de medio duplex (pyserial)."""

from __future__ import annotations

import logging

log = logging.getLogger("tmc2209")

SYNC = 0x05
# Registros usados
GCONF = 0x00
IHOLD_IRUN = 0x10
TPOWERDOWN = 0x11
TSTEP = 0x12
TCOOLTHRS = 0x14
SGTHRS = 0x40
SG_RESULT = 0x41
CHOPCONF = 0x6C
DRV_STATUS = 0x6F

# micropasos -> campo MRES de CHOPCONF (bits 24-27)
_MRES = {256: 0, 128: 1, 64: 2, 32: 3, 16: 4, 8: 5, 4: 6, 2: 7, 1: 8}
CHOPCONF_BASE = 0x10000053          # TOFF=3, valores de chopper por defecto del datasheet


def crc8(data: bytes) -> int:
    """CRC de 8 bits del datagrama UART del TMC (polinomio x^8+x^2+x+1 = 0x07, cada byte de LSB a MSB)."""
    crc = 0
    for byte in data:
        b = byte
        for _ in range(8):
            if (crc >> 7) ^ (b & 1):
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
            b >>= 1
    return crc


def write_datagram(addr: int, reg: int, value: int) -> bytes:
    """Datagrama de escritura (8 bytes): sync, nodo, reg|0x80, valor (MSB primero), CRC."""
    d = bytes([SYNC, addr & 0xFF, (reg & 0x7F) | 0x80,
               (value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF])
    return d + bytes([crc8(d)])


def read_request(addr: int, reg: int) -> bytes:
    """Datagrama de petición de lectura (4 bytes): sync, nodo, reg, CRC."""
    d = bytes([SYNC, addr & 0xFF, reg & 0x7F])
    return d + bytes([crc8(d)])


def parse_read_reply(buf: bytes) -> int | None:
    """Valor de una respuesta de lectura (8 bytes: sync, 0xFF, reg, datos, CRC). None si el CRC no cuadra o no la encuentra."""
    for i in range(0, max(1, len(buf) - 7)):
        d = buf[i:i + 8]
        if len(d) == 8 and d[0] == SYNC and d[1] == 0xFF and crc8(d[:7]) == d[7]:
            return (d[3] << 24) | (d[4] << 16) | (d[5] << 8) | d[6]
    return None


def chopconf(micro: int, base: int = CHOPCONF_BASE) -> int:
    mres = _MRES.get(int(micro), 4)
    return (base & ~(0xF << 24)) | (mres << 24)


def ihold_irun(irun: int, ihold: int, delay: int = 1) -> int:
    return (ihold & 0x1F) | ((irun & 0x1F) << 8) | ((delay & 0x0F) << 16)


# GCONF para control por UART: pdn_disable (bit 6) y mstep_reg_select (bit 7); en_spreadCycle (bit 2) opcional
def gconf(spread: bool = False) -> int:
    return 0xC0 | (0x04 if spread else 0x00)


class Tmc2209:
    """Configura un TMC2209 por un puerto serie de medio duplex (pyserial). El puerto debe estar abierto."""

    def __init__(self, port, addr: int = 0):
        self.p, self.addr = port, addr

    def write(self, reg: int, value: int):
        self.p.write(write_datagram(self.addr, reg, value))

    def read(self, reg: int) -> int | None:
        try:
            if hasattr(self.p, "reset_input_buffer"):
                self.p.reset_input_buffer()
            self.p.write(read_request(self.addr, reg))
            return parse_read_reply(self.p.read(12))          # el eco de la petición va delante de la respuesta
        except Exception as e:  # noqa: BLE001
            log.warning("TMC2209 lectura 0x%02x: %s", reg, e)
            return None

    def configure(self, irun: int, ihold: int, micro: int, sgthrs: int, tcoolthrs: int,
                  delay: int = 1, spread: bool = False):
        """Fija corriente (IRUN/IHOLD 0-31), micropasos, y StallGuard (SGTHRS 0-255, TCOOLTHRS)."""
        self.write(GCONF, gconf(spread))
        self.write(IHOLD_IRUN, ihold_irun(irun, ihold, delay))
        self.write(CHOPCONF, chopconf(micro))
        self.write(TCOOLTHRS, tcoolthrs & 0xFFFFF)
        self.write(SGTHRS, sgthrs & 0xFF)

    def sg_result(self) -> int | None:
        return self.read(SG_RESULT)


def open_port(dev: str, baud: int = 115200):
    """Abre el puerto serie del TMC2209 (solo en la Raspberry)."""
    import serial
    return serial.Serial(dev, baud, timeout=0.05)
