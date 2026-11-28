#!/usr/bin/env python3
# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
# version. It is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
"""seatel_rotctld — la PCU de una antena Sea Tel vista como un rotor de Hamlib.

Cualquier programa que hable con ``rotctld`` (Gpredict, Look4Sat, SatDump, PstRotator, ``rotctl -m 2``, el tracker y la
caja APRS LoRa+ Rotor, S.A.T.-APRS+...) mueve la antena por TCP (y, si se pide, por UDP). Stellarium y otros programas
de astronomía entran por el puente LX200 opcional, que sigue el objeto a velocidad sidérea.

Enlace con la PCU (``--pcu``):
    COM7  |  /dev/ttyUSB0        adaptador USB-RS422 (necesita pyserial)
    tcp://192.168.1.50:4001      puente serie por TCP (ser2net, ESP32, conversor Ethernet-RS422)
    udp://192.168.1.50:4001      puente serie por UDP
    sim                          PCU simulada en memoria (pruebas sin antena)

Protocolo de la PCU (9600 8N1): consulta ``P`` → ``EddddAddddCdddd`` + suma + CR; consignas ``Adddd`` y ``Edddd`` en
décimas de grado + suma + CR; suma = 0x80 | (suma de bytes & 0x7F); los bytes 0x1F de fondo se descartan.

SEGURIDAD
    * Sin ``--allow-real-motion`` solo se emite ``P``: las consignas se aceptan y se registran, pero NO se transmiten.
    * El protocolo no tiene orden de parada. ``S`` deja de enviar consignas (o, con ``--stop-mode hold``, manda como
      consigna la posición actual). Nada de esto es una parada de emergencia: cerrar el programa NO frena los motores.
    * Una consigna cuyo camino corto cruza 0/360 se rechaza (RPRT -1) salvo con ``--allow-north-crossing``.
    * Cada consigna es UNA pareja A/E, sin reintentos. Una transmisión a medias o la pérdida de telemetría con una
      consigna activa enclavan un fallo que solo se borra con la orden ``R`` de rotctl (``rotctl -m 2 R 1``).
"""

from __future__ import annotations

import argparse
import logging
import math
import re
import socket
import socketserver
import threading
import time
from collections import deque

__version__ = "1.1.0"
log = logging.getLogger("seatel")
mono = time.monotonic

# Tiempos de la transacción con la PCU (s)
P_REPLY_TIMEOUT = 0.85      # espera de la respuesta P pendiente antes de A/E
P_RECOVERY_GAP = 0.15       # separación entre la respuesta P y la consigna
AE_GAP = 0.060              # pausa entre A y E
AE_LISTEN = 0.260           # escucha pasiva tras E (la respuesta ** / *????? no está documentada: solo se registra)
POS_FRESH = 2.5             # edad máxima de la posición para transmitir
POS_VALID = 5.0             # edad máxima de la posición que se entrega a los clientes
TELEMETRY_LOST = 8.0        # sin P válida con una consigna activa → fallo
NO_MOTION = 20.0            # sin movimiento y lejos de la consigna → fallo
ARRIVE_TOL = 0.2            # grados; dos lecturas seguidas dentro = llegada
RUNAWAY = 1.0               # grados que puede crecer el error respecto al momento del envío

# Errores de Hamlib (se envían como RPRT -n)
E_OK, E_INVAL, E_NIMPL, E_IO, E_REJECTED, E_NAVAIL = 0, 1, 4, 6, 9, 11
ST_MOVING = 2


# ---- Tramas de la PCU ----------------------------------------------------------------------------------------------------
def checksum(payload: bytes) -> int:
    return 0x80 | (sum(payload) & 0x7F)


def packet(axis: str, tenths: int) -> bytes:
    """Consigna de un eje: ``A2203`` + suma + CR para 220,3°."""
    if axis not in ("A", "E") or not 0 <= tenths <= 9999:
        raise ValueError("consigna fuera de rango")
    payload = f"{axis}{tenths:04d}".encode("ascii")
    return payload + bytes((checksum(payload), 0x0D))


_P_BODY = re.compile(rb"E(\d{4})A(\d{4})C(\d{4})")


def parse_p(frame: bytes):
    """Respuesta P sin el CR → (az, el, cross, prefijo) o None. Admite bytes extra delante (``**``...)."""
    if not 16 <= len(frame) <= 64:
        return None
    body = _P_BODY.fullmatch(frame[-16:-1])
    if body is None or frame[-1] != checksum(frame[-16:-1]):
        return None
    el, az, cross = (int(g) / 10.0 for g in body.groups())
    if not (0 <= az < 360 and 0 <= el <= 90):
        return None
    return az, el, cross, frame[:-16]


def angle_diff(a: float, b: float) -> float:
    """Giro más corto de a hasta b, en (-180, 180]."""
    d = (b - a) % 360.0
    return d - 360.0 if d > 180.0 else d


# ---- Transportes: USB (serie), TCP, UDP y simulador -------------------------------------------------------------------
class SerialTransport:
    def __init__(self, device: str, baud: int):
        import serial                                       # pyserial solo hace falta para el puerto serie
        self._s = serial.Serial(device, baud, bytesize=8, parity="N", stopbits=1, timeout=0.1, write_timeout=0.3)

    def read(self) -> bytes:
        return self._s.read(512)

    def write(self, data: bytes):
        if self._s.write(data) != len(data):
            raise OSError("escritura serie incompleta")

    def flush(self):
        self._s.flush()

    def close(self):
        self._s.close()


class SocketTransport:
    """Puente serie por la red. En UDP cada escritura es un datagrama."""

    def __init__(self, host: str, port: int, udp: bool):
        if udp:
            self._s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._s.connect((host, port))
        else:
            self._s = socket.create_connection((host, port), timeout=3.0)
            self._s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._udp = udp
        self._s.settimeout(0.1)

    def read(self) -> bytes:
        try:
            data = self._s.recv(512)
        except socket.timeout:
            return b""
        if not data and not self._udp:
            raise ConnectionError("el puente TCP cerró la conexión")
        return data

    def write(self, data: bytes):
        self._s.sendall(data)

    def flush(self):
        pass

    def close(self):
        self._s.close()


class SimPcu:
    """PCU simulada: contesta a P y se mueve hacia la última consigna a ``speed`` °/s por el camino corto."""

    def __init__(self, az: float = 180.0, el: float = 45.0, speed: float = 3.0):
        self.pos, self.tgt, self.speed = [az, el], [az, el], speed
        self.mute = False                                   # True: deja de contestar a P
        self.fail_on = b""                                  # b"E": la escritura de esa consigna falla
        self.writes: list[bytes] = []
        self._rx, self._buf, self._t, self._go = deque(), bytearray(), mono(), False

    def _advance(self):
        now = mono()
        step, self._t = self.speed * (now - self._t), now
        if not self._go:                                    # en reposo no corrige: ``pos`` se puede mover a mano
            return
        da = angle_diff(self.pos[0], self.tgt[0])
        self.pos[0] = (self.pos[0] + math.copysign(min(abs(da), step), da)) % 360.0
        de = self.tgt[1] - self.pos[1]
        self.pos[1] += math.copysign(min(abs(de), step), de)
        self._go = abs(angle_diff(self.pos[0], self.tgt[0])) > 1e-9 or abs(self.tgt[1] - self.pos[1]) > 1e-9

    def write(self, data: bytes):
        self.writes.append(bytes(data))
        if data == b"P":
            self._advance()
            if not self.mute:
                body = b"E%04dA%04dC0900" % (round(self.pos[1] * 10), round(self.pos[0] * 10) % 3600)
                self._rx.append(body + bytes((checksum(body), 0x0D)))
            return
        if self.fail_on and data.startswith(self.fail_on):
            raise OSError("fallo simulado de escritura")
        for b in data:
            if b != 0x0D:
                self._buf.append(b)
                continue
            frame, self._buf = bytes(self._buf), bytearray()
            if len(frame) == 6 and frame[5] == checksum(frame[:5]) and frame[:1] in (b"A", b"E"):
                self._advance()
                self.tgt[0 if frame[:1] == b"A" else 1] = int(frame[1:5]) / 10.0
                self._go = True
                if frame[:1] == b"E":
                    self._rx.append(b"**")

    def read(self) -> bytes:
        if self._rx:
            return self._rx.popleft()
        time.sleep(0.02)
        return b""

    def flush(self):
        pass

    def close(self):
        pass


def open_transport(url: str, baud: int):
    m = re.fullmatch(r"(tcp|udp)://([^:/]+):(\d+)", url)
    if m:
        return SocketTransport(m.group(2), int(m.group(3)), m.group(1) == "udp")
    if url == "sim":
        return SimPcu()
    return SerialTransport(url, baud)


# ---- El rotor --------------------------------------------------------------------------------------------------------------
class SeaTel(threading.Thread):
    """Dueño único del enlace con la PCU. Los servidores solo llaman a set_pos / get_pos / stop / park / clear.

    Los ángulos de la interfaz son los del cliente (acimut verdadero); los internos son los de la PCU:
    PCU = cliente − offset. Los límites se dan en grados de la PCU.
    """

    def __init__(self, pcu: str, baud: int = 9600, allow_motion: bool = False,
                 min_az: float = 0.0, max_az: float = 359.9, min_el: float = 5.0, max_el: float = 85.0,
                 az_offset: float = 0.0, el_offset: float = 0.0, park=None,
                 min_interval: float = 2.0, wait_arrival: bool = False, north_crossing: bool = False,
                 stop_mode: str = "cancel", poll: float = 1.0, arrive_tol: float = ARRIVE_TOL, transport=None):
        super().__init__(name="SeaTel", daemon=True)
        if not (0 <= min_az < max_az < 360 and 0 <= min_el < max_el <= 90):
            raise ValueError("límites inválidos (AZ 0-359,9; EL 0-90)")
        self.pcu, self.baud, self.allow_motion = pcu, baud, allow_motion
        self.lim = (round(min_az * 10), round(max_az * 10), round(min_el * 10), round(max_el * 10))
        self.full_circle = self.lim[0] == 0 and self.lim[1] >= 3599
        self.az_offset, self.el_offset, self.park_pos = az_offset, el_offset, park
        self.min_interval, self.wait_arrival = max(0.5, min_interval), wait_arrival
        self.north_crossing, self.stop_mode, self.poll = north_crossing, stop_mode, max(0.2, poll)
        self.arrive_tol = min(max(arrive_tol, 0.1), 2.0)
        self._fixed_tp = transport                          # transporte ya abierto (pruebas)
        self._tp = None
        self._lock = threading.Lock()
        self._quit = threading.Event()
        self._buf = bytearray()
        self._awaiting, self._t_query, self._t_reply = False, 0.0, 0.0
        self._pos, self._t_pos = None, 0.0                  # (az, el) de la PCU y cuándo se leyó
        self._target = None                                 # consigna pedida, décimas de la PCU
        self._sent, self._t_sent = None, 0.0                # última consigna transmitida
        self._urgent = False                                # consigna de HOLD: no espera al intervalo
        self._active = self._arrived = False                # hay consigna transmitida / ya se alcanzó
        self._hits, self._d0, self._t_motion, self._last = 0, 0.0, 0.0, None
        self.fault = ""
        self.source = None                                  # quién dio la última consigna ("rotctl", "lx200")
        self.stats = dict(p_tx=0, p_ok=0, bad=0, ae_tx=0)

    # ---- interfaz para los servidores (cualquier hilo) --------------------------------------------------------------
    @property
    def limits(self):
        """(min_az, max_az, min_el, max_el) en grados del cliente."""
        a0, a1, e0, e1 = (v / 10.0 for v in self.lim)
        if self.full_circle:
            return 0.0, 360.0, e0 + self.el_offset, e1 + self.el_offset
        return a0 + self.az_offset, a1 + self.az_offset, e0 + self.el_offset, e1 + self.el_offset

    def get_pos(self):
        with self._lock:
            if self._pos is None or mono() - self._t_pos > POS_VALID:
                return None
            return (self._pos[0] + self.az_offset) % 360.0, self._pos[1] + self.el_offset

    def set_pos(self, az: float, el: float, source: str = "rotctl") -> int:
        if not (math.isfinite(az) and math.isfinite(el)):
            return E_INVAL
        a0, a1, e0, e1 = self.lim
        ta = min(round(((az - self.az_offset) % 360.0) * 10), 3599)      # 359,97 se queda en 359,9: no salta a 0
        te = min(max(round((el - self.el_offset) * 10), e0), e1)        # la elevación se recorta a los límites
        if not a0 <= ta <= a1:
            return E_INVAL
        with self._lock:
            if self.fault:
                return E_REJECTED
            if self._pos is None:
                return E_IO
            if not self.north_crossing and abs(ta / 10.0 - self._pos[0]) > 180.0:
                return E_INVAL                              # el camino corto cruza 0/360
            self._target, self._urgent, self.source = (ta, te), False, source
        return E_OK

    def stop(self) -> int:
        """Deja de enviar consignas; con ``--stop-mode hold`` manda la posición actual. NO es una parada de emergencia."""
        with self._lock:
            self._target, self._urgent, self.source = None, False, None
            hold = (self.stop_mode == "hold" and self.allow_motion and not self.fault and self._active
                    and not self._arrived and self._pos is not None and mono() - self._t_pos <= POS_FRESH)
            if hold:
                self._target = (min(round(self._pos[0] * 10), 3599), round(self._pos[1] * 10))
                self._urgent = True
            else:
                self._sent, self._active = None, False
        log.info("STOP: %s", "consigna = posición actual (HOLD)" if hold else "sin más consignas (la PCU termina la última)")
        return E_OK

    def park(self) -> int:
        return self.set_pos(*self.park_pos) if self.park_pos else E_NAVAIL

    def clear(self) -> int:
        with self._lock:
            if self.fault:
                log.warning("Fallo borrado por el operador: %s", self.fault)
            self.fault, self._target, self._sent, self._active = "", None, None, False
        return E_OK

    def moving(self) -> bool:
        with self._lock:
            return self._active and not self._arrived

    def info(self) -> str:
        mode = "movimiento real" if self.allow_motion else "ENSAYO sin TX"
        return f"SeaTel rotctld {__version__} ({mode})" + (f" FALLO: {self.fault}" if self.fault else "")

    def shutdown(self):
        self._quit.set()

    # ---- hilo del enlace ------------------------------------------------------------------------------------------------
    def run(self):
        while not self._quit.is_set():
            if self._tp is None:
                try:
                    self._tp = self._fixed_tp or open_transport(self.pcu, self.baud)
                    self._buf.clear()
                    self._awaiting, self._t_query = False, 0.0
                    log.info("PCU conectada: %s", self.pcu)
                except Exception as exc:
                    log.error("No se puede abrir %s: %s (nuevo intento en 3 s)", self.pcu, exc)
                    self._quit.wait(3.0)
                continue
            try:
                self._cycle()
            except Exception as exc:
                log.error("Enlace con la PCU perdido: %s", exc)
                self._drop_link()
        if self._tp is not None:
            self._tp.close()

    def _drop_link(self):
        try:
            self._tp.close()
        except Exception:
            pass
        self._tp = None
        with self._lock:
            if self._active and not self._arrived and not self.fault:
                self._set_fault("enlace perdido con una consigna activa")
            self._target, self._sent, self._active, self._pos = None, None, False, None
        if self._fixed_tp is not None:
            self._quit.set()
        else:
            self._quit.wait(1.0)

    def _set_fault(self, reason: str):                      # con el candado tomado
        self.fault, self._target = reason, None
        log.error("FALLO ENCLAVADO: %s. No se envían más consignas; la PCU NO se ha parado: comprobar la antena. "
                  "Borrar con la orden R de rotctl.", reason)

    def _cycle(self):
        now = mono()
        if self._awaiting and now - self._t_query > 1.5:
            self._awaiting = False
        if not self._awaiting and now - self._t_query >= self.poll:
            self._tp.write(b"P")
            self._awaiting, self._t_query = True, mono()
            self.stats["p_tx"] += 1
        self._rx()
        self._watch()
        self._tx_if_due()

    def _rx(self):
        for b in self._tp.read():
            if b == 0x1F:
                continue
            if b != 0x0D:
                if len(self._buf) >= 64:
                    self._buf.clear()
                self._buf.append(b)
                continue
            frame = bytes(self._buf)
            self._buf.clear()
            p = parse_p(frame)
            if p is None:
                if frame:
                    self.stats["bad"] += 1
                    log.debug("RX no reconocida: %s", frame.hex(" ").upper())
                continue
            az, el, _cross, prefix = p
            if prefix:
                log.debug("RX antes de P (sin significado documentado): %s", prefix.hex(" ").upper())
            self._awaiting, self._t_reply = False, mono()
            self.stats["p_ok"] += 1
            self._sample(az, el)

    def _dist(self, pos, tenths) -> float:
        return max(abs(angle_diff(pos[0], tenths[0] / 10.0)), abs(pos[1] - tenths[1] / 10.0))

    def _sample(self, az: float, el: float):
        now = mono()
        with self._lock:
            self._pos, self._t_pos = (az, el), now
            if self._last is None or abs(angle_diff(self._last[0], az)) >= 0.1 or abs(self._last[1] - el) >= 0.1:
                self._last, self._t_motion = (az, el), now
            if not self._active or self._arrived or self.fault:
                return
            d = self._dist((az, el), self._sent)
            if d <= self.arrive_tol + 1e-6:
                self._hits += 1
                if self._hits >= 2:
                    self._arrived = True
                    log.info("Consigna alcanzada: AZ %.1f EL %.1f", az, el)
            else:
                self._hits = 0
                if d > self._d0 + RUNAWAY:
                    self._set_fault(f"la antena se aleja de la consigna (error {d:.1f}°)")

    def _watch(self):
        now = mono()
        with self._lock:
            if not self._active or self._arrived or self.fault:
                return
            if now - self._t_pos > TELEMETRY_LOST:
                self._set_fault(f"sin telemetría P durante {TELEMETRY_LOST:g} s con una consigna activa")
            elif now - self._t_motion > NO_MOTION and now - self._t_sent > NO_MOTION:
                self._set_fault(f"sin movimiento durante {NO_MOTION:g} s lejos de la consigna")

    def _tx_if_due(self):
        now = mono()
        with self._lock:
            tgt, urgent = self._target, self._urgent
            if tgt is None or tgt == self._sent or self.fault:
                return
            if not urgent:
                if now - self._t_sent < self.min_interval:
                    return
                if self.wait_arrival and self._active and not self._arrived:
                    return
            if self._pos is None or now - self._t_pos > POS_FRESH:
                return
            cur = self._pos
            if not self.north_crossing and abs(tgt[0] / 10.0 - cur[0]) > 180.0:
                log.warning("Consigna AZ %.1f rechazada: el camino corto cruza 0/360 (ver --allow-north-crossing)",
                            tgt[0] / 10.0)
                self._target = None
                return
            pa, pe = packet("A", tgt[0]), packet("E", tgt[1])
            if not self.allow_motion:
                log.info("ENSAYO SIN TX → AZ %.1f EL %.1f | %s / %s", tgt[0] / 10.0, tgt[1] / 10.0,
                         pa.hex(" ").upper(), pe.hex(" ").upper())
                self._sent, self._t_sent = tgt, now
                return
        # Nunca A/E con una consulta P a medias: se espera su respuesta y se deja un hueco.
        deadline = mono() + P_REPLY_TIMEOUT
        while self._awaiting and mono() < deadline:
            self._rx()
        if self._awaiting:
            return                                          # nada transmitido: se intenta en la próxima vuelta
        gap = P_RECOVERY_GAP - (mono() - self._t_reply)
        if gap > 0:
            time.sleep(gap)
        with self._lock:
            if self._target != tgt or self.fault:           # cancelada o cambiada mientras se esperaba
                return
        try:
            self._tp.write(pa)
            self._tp.flush()
            time.sleep(AE_GAP)
            self._tp.write(pe)
            self._tp.flush()
        except Exception as exc:
            with self._lock:
                self._set_fault(f"transmisión A/E incompleta ({exc}); estado de la PCU incierto")
            raise
        now = mono()
        with self._lock:
            if not self._active or self._arrived:
                self._t_motion = now
            self._sent, self._t_sent, self._urgent = tgt, now, False
            self._active, self._arrived, self._hits = True, False, 0
            self._d0 = self._dist(cur, tgt)
        self.stats["ae_tx"] += 1
        log.info("TX A/E → AZ %.1f EL %.1f | %s / %s", tgt[0] / 10.0, tgt[1] / 10.0,
                 pa.hex(" ").upper(), pe.hex(" ").upper())
        end = mono() + AE_LISTEN                            # escucha pasiva; jamás se reintenta
        while mono() < end:
            self._rx()


# ---- Servidor rotctld (protocolo de red de Hamlib 4.x) --------------------------------------------------------------
_SHORT = {"P": "set_pos", "p": "get_pos", "S": "stop", "K": "park", "R": "reset", "M": "move", "s": "get_status",
          "_": "get_info", "1": "dump_caps", "q": "quit", "Q": "quit"}


def _rprt(code: int) -> str:
    return f"RPRT {-code}\n"


def rotctl_line(raw: str, rot: SeaTel) -> tuple[str, bool]:
    """Una línea de rotctl → (respuesta, cerrar la conexión).

    Con ``+`` delante (y ``;``, ``|`` o ``,`` como separador opcional) contesta en modo extendido:
    ``orden:`` + líneas de la respuesta + ``RPRT n``.
    """
    raw = raw.strip()
    ext = raw.startswith("+")
    sep = "\n"
    if ext:
        if raw[1:2] in (";", "|", ","):
            sep, raw = raw[1], raw[2:].strip()
        else:
            raw = raw[1:].strip()
    if not raw:
        return "", False
    if raw[0] == "\\":
        cmd, *args = raw[1:].split() or [""]
    elif raw[0] in _SHORT:
        cmd, args = _SHORT[raw[0]], raw[1:].split()
    else:
        return _rprt(E_NIMPL), False
    reply, close = _rotctl(cmd, args, rot)
    if not ext:
        return reply, close
    lines, end = ([], reply) if reply.startswith("RPRT") else (reply.splitlines(), _rprt(E_OK))
    return f"{cmd}:{sep}" + "".join(line + sep for line in lines) + end, close


def _rotctl(cmd: str, args: list, rot: SeaTel) -> tuple[str, bool]:
    if cmd == "set_pos":
        try:
            az, el = float(args[0].replace(",", ".")), float(args[1].replace(",", "."))
        except (IndexError, ValueError):
            return _rprt(E_INVAL), False
        if not (-360 <= az <= 720 and -90 <= el <= 180):
            return _rprt(E_INVAL), False
        return _rprt(rot.set_pos(az, el)), False
    if cmd == "get_pos":
        pos = rot.get_pos()
        return (_rprt(E_IO) if pos is None else f"{pos[0]:.6f}\n{pos[1]:.6f}\n"), False
    if cmd == "stop":
        return _rprt(rot.stop()), False
    if cmd == "reset":
        return _rprt(rot.clear()), False
    if cmd == "park":
        return _rprt(rot.park()), False
    if cmd == "move":
        return _rprt(E_NAVAIL), False                       # sin orden de parada no hay movimiento manual seguro
    if cmd == "get_status":
        return f"{ST_MOVING if rot.moving() else 0}\n", False        # rot_status_t de Hamlib, en número
    if cmd == "get_info":
        return rot.info() + "\n", False
    if cmd == "dump_state":
        a0, a1, e0, e1 = rot.limits
        return (f"1\n2\nmin_az={a0:.6f}\nmax_az={a1:.6f}\nmin_el={e0:.6f}\nmax_el={e1:.6f}\n"
                "south_zero=0\nrot_type=AzEl\ndone\n"), False
    if cmd == "dump_caps":
        a0, a1, e0, e1 = rot.limits
        return (f"Model name:\tSea Tel PCU\nMfg name:\tEA7JRS\nMin Azimuth:\t{a0:.2f}\nMax Azimuth:\t{a1:.2f}\n"
                f"Min Elevation:\t{e0:.2f}\nMax Elevation:\t{e1:.2f}\n"), False
    if cmd == "chk_vfo":
        return "0\n", False
    if cmd == "quit":
        return _rprt(E_OK), True
    return _rprt(E_NIMPL), False


class _Tcp(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class _Udp(socketserver.ThreadingUDPServer):
    allow_reuse_address = True
    daemon_threads = True


def serve_rotctl(rot: SeaTel, host: str, port: int, udp_port: int = 0) -> list:
    class TcpHandler(socketserver.StreamRequestHandler):
        def handle(self):
            log.info("rotctl: cliente %s conectado", self.client_address[0])
            try:
                for line in self.rfile:
                    reply, close = rotctl_line(line.decode("ascii", "replace"), rot)
                    if reply:
                        self.wfile.write(reply.encode())
                    if close:
                        break
            except OSError:
                pass
            log.info("rotctl: cliente %s desconectado", self.client_address[0])

    class UdpHandler(socketserver.BaseRequestHandler):
        def handle(self):
            data, sock = self.request
            out = "".join(rotctl_line(l, rot)[0] for l in data.decode("ascii", "replace").splitlines())
            if out:
                sock.sendto(out.encode(), self.client_address)

    servers = [_Tcp((host, port), TcpHandler)]
    if udp_port:
        servers.append(_Udp((host, udp_port), UdpHandler))
    for s in servers:
        threading.Thread(target=s.serve_forever, daemon=True, name="rotctl").start()
    return servers


# ---- Puente LX200 para programas de astronomía (Stellarium) ---------------------------------------------------------
def _precession(ra: float, dec: float, jd: float, to_date: bool) -> tuple[float, float]:
    """Precesión IAU 1976 entre J2000 y la fecha (radianes)."""
    t = (jd - 2451545.0) / 36525.0
    sec = math.radians(1 / 3600.0)
    zeta = (2306.2181 * t + 0.30188 * t * t + 0.017998 * t ** 3) * sec
    z = (2306.2181 * t + 1.09468 * t * t + 0.018203 * t ** 3) * sec
    theta = (2004.3109 * t - 0.42665 * t * t - 0.041833 * t ** 3) * sec
    if not to_date:
        zeta, z, theta = -z, -zeta, -theta
    a = math.cos(dec) * math.sin(ra + zeta)
    b = math.cos(theta) * math.cos(dec) * math.cos(ra + zeta) - math.sin(theta) * math.sin(dec)
    c = math.sin(theta) * math.cos(dec) * math.cos(ra + zeta) + math.cos(theta) * math.sin(dec)
    return (math.atan2(a, b) + z) % (2 * math.pi), math.asin(max(-1.0, min(1.0, c)))


def _lst(jd: float, lon_deg: float) -> float:
    t = (jd - 2451545.0) / 36525.0
    gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t - t ** 3 / 38710000.0
    return math.radians((gmst + lon_deg) % 360.0)


def radec_to_azel(ra_h: float, dec_deg: float, lat: float, lon: float, unix: float, j2000: bool = True):
    """AR (horas) / Dec (grados) → acimut y elevación geométricos (sin refracción), en grados."""
    jd = unix / 86400.0 + 2440587.5
    ra, dec = math.radians(ra_h * 15.0), math.radians(dec_deg)
    if j2000:
        ra, dec = _precession(ra, dec, jd, True)
    h, phi = _lst(jd, lon) - ra, math.radians(lat)
    el = math.asin(math.sin(phi) * math.sin(dec) + math.cos(phi) * math.cos(dec) * math.cos(h))
    az = math.atan2(-math.cos(dec) * math.sin(h), math.sin(dec) * math.cos(phi) - math.cos(dec) * math.sin(phi) * math.cos(h))
    return math.degrees(az) % 360.0, math.degrees(el)


def azel_to_radec(az_deg: float, el_deg: float, lat: float, lon: float, unix: float, j2000: bool = True):
    jd = unix / 86400.0 + 2440587.5
    az, el, phi = math.radians(az_deg), math.radians(el_deg), math.radians(lat)
    dec = math.asin(math.sin(el) * math.sin(phi) + math.cos(el) * math.cos(phi) * math.cos(az))
    h = math.atan2(-math.sin(az) * math.cos(el), math.sin(el) * math.cos(phi) - math.cos(el) * math.sin(phi) * math.cos(az))
    ra = (_lst(jd, lon) - h) % (2 * math.pi)
    if j2000:
        ra, dec = _precession(ra, dec, jd, False)
    return math.degrees(ra) / 15.0, math.degrees(dec)


class Lx200Bridge:
    """Subconjunto LX200 (:Sr :Sd :MS :Q :GR :GD). Tras :MS# sigue el objeto recalculando AZ/EL cada segundo."""

    def __init__(self, rot: SeaTel, lat: float, lon: float, j2000: bool = True):
        self.rot, self.lat, self.lon, self.j2000 = rot, lat, lon, j2000
        self.target = None                                  # (AR horas, Dec grados)
        threading.Thread(target=self._track, daemon=True, name="lx200-track").start()

    def _azel(self):
        return radec_to_azel(*self.target, self.lat, self.lon, time.time(), self.j2000)

    def goto(self, ra: float, dec: float) -> str:
        self.target = (ra, dec)
        az, el = self._azel()
        if el < self.rot.limits[2]:
            self.target = None
            return "1Bajo el limite de elevacion#"
        code = self.rot.set_pos(az, el, "lx200")
        if code != E_OK:
            self.target = None
            return "2Consigna rechazada#"
        log.info("LX200 GOTO AR %.4f h Dec %+.4f° → AZ %.2f EL %.2f", ra, dec, az, el)
        return "0"

    def cancel(self):
        self.target = None
        self.rot.stop()

    def _track(self):
        while True:
            time.sleep(1.0)
            if self.target is None:
                continue
            if self.rot.source != "lx200":                  # otro cliente tomó el mando, o hubo STOP
                self.target = None
                continue
            az, el = self._azel()
            if el < self.rot.limits[2] or self.rot.set_pos(az, el, "lx200") != E_OK:
                log.warning("LX200: seguimiento terminado (objeto bajo el límite o consigna rechazada)")
                self.target = None

    def command(self, cmd: str, st: dict) -> str:
        if cmd.startswith(":Sr"):
            p = [float(x) for x in cmd[3:].split(":")]
            ra = p[0] + p[1] / 60.0 + (p[2] / 3600.0 if len(p) > 2 else 0.0)
            if not 0 <= ra < 24:
                return "0"
            st["ra"] = ra
            return "1"
        if cmd.startswith(":Sd"):
            v = cmd[3:].replace("*", ":").replace("'", ":").strip()
            p = [float(x) for x in v.lstrip("+-").split(":")]
            dec = (p[0] + p[1] / 60.0 + (p[2] / 3600.0 if len(p) > 2 else 0.0)) * (-1 if v.startswith("-") else 1)
            if not -90 <= dec <= 90:
                return "0"
            st["dec"] = dec
            return "1"
        if cmd == ":MS":
            return self.goto(st["ra"], st["dec"]) if "ra" in st and "dec" in st else "1Sin coordenadas#"
        if cmd == ":Q":
            self.cancel()
            return ""
        if cmd in (":GR", ":GD"):
            pos = self.rot.get_pos()
            if pos is not None:
                ra, dec = azel_to_radec(*pos, self.lat, self.lon, time.time(), self.j2000)
            else:
                ra, dec = self.target or (0.0, 0.0)
            if cmd == ":GR":
                s = round(ra * 3600) % 86400
                return f"{s // 3600:02d}:{s % 3600 // 60:02d}:{s % 60:02d}#"
            s = round(abs(dec) * 3600)
            return f"{'-' if dec < 0 else '+'}{s // 3600:02d}*{s % 3600 // 60:02d}:{s % 60:02d}#"
        return ""

    def serve(self, host: str, port: int):
        bridge = self

        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                st, buf = {}, bytearray()
                while True:
                    try:
                        data = self.request.recv(512)
                    except OSError:
                        return
                    if not data:
                        return
                    buf.extend(data)
                    while b"#" in buf:
                        raw, _, rest = bytes(buf).partition(b"#")
                        buf = bytearray(rest)
                        cmd = raw.replace(b"\xdf", b"*").decode("ascii", "ignore").strip().lstrip("\x06")
                        if not cmd:
                            continue
                        try:
                            reply = bridge.command(cmd, st)
                        except (ValueError, IndexError) as exc:
                            log.debug("LX200 %r: %s", cmd, exc)
                            reply = "0" if cmd[:3] in (":Sr", ":Sd") else ""
                        if reply:
                            self.request.sendall(reply.encode())

        server = _Tcp((host, port), Handler)
        threading.Thread(target=server.serve_forever, daemon=True, name="lx200").start()
        return server


# ---- Programa ------------------------------------------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Servidor rotctld (Hamlib) para la PCU de una antena Sea Tel.")
    ap.add_argument("--pcu", required=True, help="COM7 | /dev/ttyUSB0 | tcp://host:puerto | udp://host:puerto | sim")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--allow-real-motion", action="store_true", help="transmitir consignas A/E (sin esto, solo ensayo)")
    ap.add_argument("--listen", default="127.0.0.1", help="dirección de escucha; 0.0.0.0 abre el mando a toda la red")
    ap.add_argument("--port", type=int, default=4533, help="rotctld por TCP")
    ap.add_argument("--udp-port", type=int, default=0, help="las mismas órdenes de rotctl por UDP (0 = apagado)")
    ap.add_argument("--min-az", type=float, default=0.0)
    ap.add_argument("--max-az", type=float, default=359.9)
    ap.add_argument("--min-el", type=float, default=5.0)
    ap.add_argument("--max-el", type=float, default=85.0)
    ap.add_argument("--az-offset", type=float, default=0.0, help="acimut verdadero − acimut de la PCU")
    ap.add_argument("--el-offset", type=float, default=0.0)
    ap.add_argument("--park", type=float, nargs=2, metavar=("AZ", "EL"))
    ap.add_argument("--min-interval", type=float, default=2.0, help="segundos mínimos entre consignas")
    ap.add_argument("--wait-arrival", action="store_true", help="no enviar otra consigna hasta alcanzar la anterior")
    ap.add_argument("--allow-north-crossing", action="store_true", help="aceptar consignas cuyo camino corto cruza 0/360")
    ap.add_argument("--stop-mode", choices=("cancel", "hold"), default="cancel",
                    help="S de rotctl: dejar de enviar consignas, o mandar la posición actual")
    ap.add_argument("--poll", type=float, default=1.0, help="segundos entre consultas P")
    ap.add_argument("--arrive-tol", type=float, default=ARRIVE_TOL, help="grados para dar una consigna por alcanzada")
    ap.add_argument("--lx200", type=int, default=0, metavar="PUERTO", help="puente LX200 para Stellarium (p. ej. 10001)")
    ap.add_argument("--lat", type=float, help="latitud de la estación (LX200)")
    ap.add_argument("--lon", type=float, help="longitud de la estación, este positivo (LX200)")
    ap.add_argument("--lx200-epoch", choices=("j2000", "jnow"), default="j2000")
    ap.add_argument("--log", help="archivo de registro")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args(argv)
    if a.lx200 and (a.lat is None or a.lon is None):
        ap.error("--lx200 necesita --lat y --lon")

    handlers = [logging.StreamHandler()] + ([logging.FileHandler(a.log, encoding="utf-8")] if a.log else [])
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO, handlers=handlers,
                        format="%(asctime)s.%(msecs)03d %(levelname)s %(message)s", datefmt="%H:%M:%S")
    rot = SeaTel(a.pcu, a.baud, a.allow_real_motion, a.min_az, a.max_az, a.min_el, a.max_el, a.az_offset, a.el_offset,
                 tuple(a.park) if a.park else None, a.min_interval, a.wait_arrival, a.allow_north_crossing,
                 a.stop_mode, a.poll, a.arrive_tol)
    rot.start()
    servers = serve_rotctl(rot, a.listen, a.port, a.udp_port)
    if a.lx200:
        servers.append(Lx200Bridge(rot, a.lat, a.lon, a.lx200_epoch == "j2000").serve(a.listen, a.lx200))
    log.info("seatel_rotctld %s — rotctld en %s:%d%s%s", __version__, a.listen, a.port,
             f", UDP {a.udp_port}" if a.udp_port else "", f", LX200 {a.lx200}" if a.lx200 else "")
    log.warning("MOVIMIENTO REAL HABILITADO. Este programa no es una parada de emergencia." if a.allow_real_motion
                else "ENSAYO: solo consultas P; las consignas se registran y NO se transmiten.")
    try:
        while rot.is_alive():
            time.sleep(0.5)
    except KeyboardInterrupt:
        log.warning("Cerrando: no se envían más consignas; la PCU termina la última que recibió.")
    for s in servers:
        s.shutdown()
    rot.shutdown()
    rot.join(2.0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
