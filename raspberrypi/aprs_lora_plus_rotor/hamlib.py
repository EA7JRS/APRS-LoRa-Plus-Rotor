# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""El rotor real: el rotctld de Hamlib (cualquier modelo de rotor de Hamlib) arrancado por el demonio en 127.0.0.1 y un
cliente que lo usa. El resto del demonio ve un :class:`HamlibRotor` con la interfaz comun (protocols.Rotor)."""

from __future__ import annotations

import asyncio
import logging
import shutil
import time

from .protocols import (MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT, MOVE_UP, ST_BUSY, ST_MOVING, ST_MOVING_AZ, ST_MOVING_EL,
                        Rotor, angle_diff)

log = logging.getLogger("hamlib")


class RotctldProcess:
    """Arranca y vigila ``rotctld -m MODELO -r PUERTO -s BAUDIOS -T 127.0.0.1 -t 4535``."""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.proc: asyncio.subprocess.Process | None = None
        self.task: asyncio.Task | None = None
        self.last_error = ""

    def argv(self) -> list[str]:
        h = self.cfg
        exe = h.get("rotctld") or shutil.which("rotctld") or "rotctld"
        a = [exe, "-m", str(h["model"]), "-T", "127.0.0.1", "-t", str(h["port"])]
        if int(h["model"]) not in (1, 2):           # 1 = Dummy (pruebas), 2 = NET rotctl: sin puerto serie
            a += ["-r", h["device"], "-s", str(h["speed"])]
        elif int(h["model"]) == 2 and h.get("device"):
            a += ["-r", h["device"]]
        if h.get("conf"):
            a += ["-C", h["conf"]]
        return a

    async def run(self):
        backoff = 2
        while True:
            argv = self.argv()
            try:
                log.info("arrancando %s", " ".join(argv))
                self.proc = await asyncio.create_subprocess_exec(*argv, stdout=asyncio.subprocess.DEVNULL,
                                                                 stderr=asyncio.subprocess.PIPE)
                err = await self.proc.stderr.read()
                rc = await self.proc.wait()
                tail = err.decode(errors="replace").strip().splitlines()
                self.last_error = tail[-1] if tail else f"exit {rc}"
                log.warning("rotctld termino (%s): %s", rc, self.last_error)
            except FileNotFoundError:
                self.last_error = "rotctld no instalado (apt install libhamlib-utils)"
                log.error(self.last_error)
            except asyncio.CancelledError:
                await self.stop()
                raise
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, 30)

    def start(self):
        self.task = asyncio.create_task(self.run())

    async def stop(self):
        if self.proc and self.proc.returncode is None:
            self.proc.terminate()
            try:
                await asyncio.wait_for(self.proc.wait(), 3)
            except asyncio.TimeoutError:
                self.proc.kill()


class HamlibRotor(Rotor):
    """Cliente del rotctld local. Los metodos de control no bloquean: encolan la orden (la ultima posicion pedida manda)."""

    def __init__(self, cfg: dict, park: tuple[float, float] = (0.0, 0.0)):
        self.cfg = cfg
        self.park_pos = park
        self.az = self.el = 0.0
        self.taz: float | None = None
        self.tel: float | None = None
        self.known = False
        self.connected = False
        self.last_reply = 0.0
        self.last_move = 0.0
        self.manual = 0
        self._pending_pos: tuple[float, float] | None = None
        self._queue: asyncio.Queue[str] = asyncio.Queue()
        self._wake = asyncio.Event()
        self._status_ok = True
        self._hw_status = 0
        self.model_info = ""
        lim = cfg
        self.min_az = float(lim.get("minAz") if lim.get("minAz") is not None else 0)
        self.max_az = float(lim.get("maxAz") if lim.get("maxAz") is not None else 360)
        self.min_el = float(lim.get("minEl") if lim.get("minEl") is not None else 0)
        self.max_el = float(lim.get("maxEl") if lim.get("maxEl") is not None else 90)
        self.has_el = True if cfg.get("hasEl") is None else bool(cfg["hasEl"])

    # ---- interfaz Rotor -------------------------------------------------------------------------------------------------
    def set_pos(self, az: float, el: float) -> bool:
        if not self.has_el:
            el = 0.0
        self.taz, self.tel, self.manual = az, el, 0
        self._pending_pos = (az, el)
        self._wake.set()
        return True

    def set_az(self, az: float) -> bool:
        return self.set_pos(az, self.el)

    def get_pos(self) -> tuple[float, float]:
        return self.az, (self.el if self.has_el else 0.0)

    def stop(self) -> bool:
        self.taz = self.tel = None
        self.manual = 0
        self._pending_pos = None
        self._send("S")
        return True

    def park(self) -> bool:
        return self.set_pos(*self.park_pos)

    def move(self, direction: int, speed: int) -> bool:
        if direction in (MOVE_UP, MOVE_DOWN) and not self.has_el:
            return False
        if direction not in (MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT):
            return False
        self.taz = self.tel = None
        self._pending_pos = None
        self.manual = direction
        self._send(f"M {direction} {speed if speed > 0 else -1}")
        return True

    def status(self) -> int:
        s = self._hw_status
        if time.monotonic() - self.last_move < 1.5 or self.manual:
            s |= ST_MOVING
        if self.taz is not None and abs(angle_diff(self.az % 360, self.taz % 360)) > 2.0:
            s |= ST_BUSY | ST_MOVING_AZ
        if self.has_el and self.tel is not None and abs(self.tel - self.el) > 2.0:
            s |= ST_BUSY | ST_MOVING_EL
        return s

    def info(self) -> str:
        return f"APRS LoRa+ Rotor (Hamlib {self.cfg.get('model')}{': ' + self.model_info if self.model_info else ''})"

    @property
    def link_ok(self) -> bool:
        return self.connected and time.monotonic() - self.last_reply < 5

    # ---- comunicacion ---------------------------------------------------------------------------------------------------
    def _send(self, line: str):
        if self._queue.qsize() < 32:
            self._queue.put_nowait(line)
        self._wake.set()

    async def _cmd(self, reader, writer, line: str, nlines: int = 1, timeout: float = 3.0) -> list[str]:
        writer.write((line + "\n").encode())
        await writer.drain()
        out = []
        while len(out) < nlines:
            raw = await asyncio.wait_for(reader.readline(), timeout)
            if not raw:
                raise ConnectionError("rotctld cerro la conexion")
            s = raw.decode(errors="replace").strip()
            out.append(s)
            if s.startswith("RPRT"):
                break
        return out

    async def _dump_state(self, reader, writer):
        writer.write(b"\\dump_state\n")
        await writer.drain()
        lines = []
        for _ in range(40):
            s = (await asyncio.wait_for(reader.readline(), 3)).decode(errors="replace").strip()
            lines.append(s)
            if s == "done" or s.startswith("RPRT"):
                break
        vals = {}
        for s in lines[2:]:
            if "=" in s:
                k, v = s.split("=", 1)
                vals[k] = v
        if len(lines) >= 6 and "=" not in lines[2]:            # protocolo 0: cuatro numeros sin etiqueta
            vals = dict(min_az=lines[2], max_az=lines[3], min_el=lines[4], max_el=lines[5])
        try:
            if self.cfg.get("minAz") is None and "min_az" in vals:
                self.min_az = float(vals["min_az"])
            if self.cfg.get("maxAz") is None and "max_az" in vals:
                self.max_az = float(vals["max_az"])
            if self.cfg.get("minEl") is None and "min_el" in vals:
                self.min_el = float(vals["min_el"])
            if self.cfg.get("maxEl") is None and "max_el" in vals:
                self.max_el = float(vals["max_el"])
            if "rot_type" in vals and self.cfg.get("hasEl") is None:
                self.has_el = vals["rot_type"] != "Az"
        except ValueError:
            pass

    async def run(self):
        host, port = "127.0.0.1", int(self.cfg["port"])
        poll = max(0.2, float(self.cfg.get("pollMs", 500)) / 1000)
        while True:
            writer = None
            try:
                reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), 3)
                self.connected = True
                await self._dump_state(reader, writer)
                info = await self._cmd(reader, writer, "_")
                self.model_info = info[0] if info and not info[0].startswith("RPRT") else ""
                log.info("rotctld conectado: AZ %.0f-%.0f EL %.0f-%.0f", self.min_az, self.max_az, self.min_el, self.max_el)
                next_poll = 0.0
                while True:
                    if self._pending_pos is not None:
                        az, el = self._pending_pos
                        self._pending_pos = None
                        r = await self._cmd(reader, writer, f"P {az:.2f} {el:.2f}")
                        if r and r[-1] != "RPRT 0":
                            log.warning("P %.1f %.1f -> %s", az, el, r[-1])
                    while not self._queue.empty():
                        await self._cmd(reader, writer, self._queue.get_nowait())
                    if time.monotonic() >= next_poll:
                        next_poll = time.monotonic() + poll
                        r = await self._cmd(reader, writer, "p", 2)
                        if len(r) == 2 and not r[0].startswith("RPRT"):
                            a, e = float(r[0]), float(r[1])
                            if self.known and (abs(a - self.az) > 0.4 or abs(e - self.el) > 0.4):
                                self.last_move = time.monotonic()
                            self.az, self.el, self.known = a, e, True
                            self.last_reply = time.monotonic()
                            if self.manual and time.monotonic() - self.last_move > 3:
                                self.manual = 0      # tope o parado por el controlador
                        if self._status_ok:
                            r = await self._cmd(reader, writer, "s")
                            if r and r[0].startswith("RPRT"):
                                self._status_ok = False
                                self._hw_status = 0
                            else:
                                self._hw_status = _parse_status(r[0] if r else "")
                    self._wake.clear()
                    try:
                        await asyncio.wait_for(self._wake.wait(), max(0.02, next_poll - time.monotonic()))
                    except asyncio.TimeoutError:
                        pass
            except asyncio.CancelledError:
                raise
            except (OSError, ConnectionError, asyncio.TimeoutError, ValueError) as e:
                if self.connected:
                    log.warning("rotctld: %s", e)
                self.connected = False
                if writer:
                    writer.close()
                await asyncio.sleep(1)


def _parse_status(s: str) -> int:
    from .protocols import STATUS_NAMES
    names = dict((n, f) for f, n in STATUS_NAMES)
    v = 0
    for w in s.split():
        v |= names.get(w, 0)
    return v
