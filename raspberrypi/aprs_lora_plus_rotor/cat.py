# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""CAT de la emisora o del receptor SDR en la Raspberry Pi (lo mismo que rc_cat.h del firmware).

- ``hamlib``: el rigctld real de Hamlib (cualquier emisora de Hamlib: Icom, Yaesu, Kenwood, Elecraft...) en 127.0.0.1:4536,
  arrancado y vigilado por el demonio, y un cliente que lo usa.
- ``rigctl``: cliente de un rigctld de la red (SDR++, gqrx, otro rigctld).
- ``sdrconnect``: SDRconnect de SDRplay (RSPdx-R2, RSP1B...) por su API WebSocket (puerto 5454); puede ir en la misma Pi.

Encima, el servidor rigctld público (4532) para GPredict, WSJT-X..., las órdenes ``cat`` / ``catset`` / ``cattrack`` del
protocolo JSON y el seguimiento con Doppler (:class:`CatTracker`)."""

from __future__ import annotations

import asyncio
import base64
import json
import logging
import os
import shutil
import time

log = logging.getLogger("cat")

C_KMS = 299792.458


def norm_mode(m: str) -> str:
    u = "".join(c for c in str(m).upper() if c not in "-_ ")
    table = {"DIG": "PKTUSB", "DATA": "PKTUSB", "DATAU": "PKTUSB", "USBD": "PKTUSB", "PKT": "PKTUSB", "DIGU": "PKTUSB",
             "DATAL": "PKTLSB", "LSBD": "PKTLSB", "DIGL": "PKTLSB", "DATAFM": "PKTFM", "FMD": "PKTFM",
             "FMN": "FM", "NFM": "FM", "AMN": "AM", "CWU": "CW", "CWL": "CWR", "CWREV": "CWR", "FSK": "RTTY",
             "FSKR": "RTTYR", "RTTYREV": "RTTYR"}
    return table.get(u, u)


def passband(m: str) -> int:
    if m in ("FM", "PKTFM"):
        return 15000
    if m == "WFM":
        return 230000
    if m == "AM":
        return 6000
    if m in ("CW", "CWR"):
        return 500
    if m.startswith("PKT"):
        return 3000
    return 2400


def smeter_db(s: int) -> int:
    """dB sobre S9 como STRENGTH de Hamlib (S9 = 0, S0 = -54)."""
    if s < 0:
        return -54
    return (s - 9) * 6 if s <= 9 else (s - 9) * 10


def smeter_from_db(db: float) -> int:
    s = round(9 + db / 6.0) if db <= 0 else round(9 + db / 10.0)
    return max(0, min(15, int(s)))


def doppler_rx(f: int, rr: float) -> int:
    return int(round(f * (1.0 - rr / C_KMS)))


def doppler_tx(f: int, rr: float) -> int:
    return int(round(f * (1.0 + rr / C_KMS)))


class RigState:
    def __init__(self):
        self.link = False
        self.freq = 0
        self.tx_freq = 0
        self.mode = ""
        self.tx_mode = ""
        self.vfo = "A"
        self.ptt = False
        self.split = False
        self.smeter = -1


class Rig:
    """Interfaz común (como RC::Rig)."""

    receiver = False
    name = ""

    def __init__(self):
        self.st = RigState()
        self.allow_ptt = False

    def set_freq(self, hz: int) -> bool: return False
    def set_mode(self, mode: str) -> bool: return False
    def set_tx_freq(self, hz: int) -> bool: return False
    def set_split(self, on: bool) -> bool: return False
    def toggle_vfo(self) -> bool: return False

    def set_vfo(self, v: str) -> bool:
        return True if v == self.st.vfo else self.toggle_vfo()

    def set_ptt(self, on: bool) -> bool:
        if on and (not self.allow_ptt or self.receiver):
            return False
        return self._ptt(on)

    def _ptt(self, on: bool) -> bool: return False

    def info(self) -> str:
        return self.name


# ---------------------------------------------------------------------------------------------------------------------- rigctl
class RigctlClientRig(Rig):
    """Cliente de rigctld: el interno de Hamlib o uno de la red (SDR++, gqrx...)."""

    def __init__(self, host: str, port: int, name: str = "", poll_ms: int = 300):
        super().__init__()
        self.host, self.port, self.name = host, int(port), name
        self.poll = max(0.1, poll_ms / 1000)
        self._q: asyncio.Queue[str] = asyncio.Queue()
        self._wake = asyncio.Event()
        self._no = set()                # consultas que el servidor no admite

    def _send(self, line: str):
        if self._q.qsize() < 32:
            self._q.put_nowait(line)
        self._wake.set()

    def set_freq(self, hz: int) -> bool:
        self.st.freq = int(hz)
        self._send(f"F {int(hz)}")
        return True

    def set_mode(self, mode: str) -> bool:
        m = norm_mode(mode)
        self.st.mode = m
        self._send(f"M {m} 0")
        return True

    def set_tx_freq(self, hz: int) -> bool:
        self.st.tx_freq = int(hz)
        self._send(f"I {int(hz)}")
        return True

    def set_split(self, on: bool) -> bool:
        self.st.split = on
        self._send("S 1 VFOB" if on else "S 0 VFOA")
        return True

    def toggle_vfo(self) -> bool:
        self.st.vfo = "B" if self.st.vfo == "A" else "A"
        self._send("V VFOB" if self.st.vfo == "B" else "V VFOA")
        return True

    def _ptt(self, on: bool) -> bool:
        self.st.ptt = on
        self._send("T 1" if on else "T 0")
        return True

    async def _cmd(self, r, w, line: str, n: int = 1) -> list[str]:
        w.write((line + "\n").encode())
        await w.drain()
        out = []
        while len(out) < n:
            raw = await asyncio.wait_for(r.readline(), 3)
            if not raw:
                raise ConnectionError("rigctld cerró la conexión")
            s = raw.decode(errors="replace").strip()
            out.append(s)
            if s.startswith("RPRT"):
                break
        return out

    async def _query(self, r, w, key: str, line: str, n: int = 1) -> list[str] | None:
        if key in self._no:
            return None
        res = await self._cmd(r, w, line, n)
        if res and res[0].startswith("RPRT") and res[0] != "RPRT 0":
            self._no.add(key)
            return None
        return res

    async def run(self):
        while True:
            w = None
            try:
                r, w = await asyncio.wait_for(asyncio.open_connection(self.host, self.port), 3)
                self._no.clear()
                log.info("rigctl conectado a %s:%s", self.host, self.port)
                while True:
                    while not self._q.empty():
                        res = await self._cmd(r, w, self._q.get_nowait())
                        if res and res[-1] != "RPRT 0":
                            log.info("rigctl: %s", res[-1])
                    f = await self._query(r, w, "f", "f")
                    if f and f[0].replace(".", "", 1).isdigit():
                        self.st.freq = int(float(f[0]))
                    m = await self._query(r, w, "m", "m", 2)
                    if m:
                        self.st.mode = norm_mode(m[0])
                    t = await self._query(r, w, "t", "t")
                    if t:
                        self.st.ptt = t[0].strip() not in ("0", "")
                    lv = await self._query(r, w, "l", "l STRENGTH")
                    if lv:
                        try:
                            self.st.smeter = smeter_from_db(float(lv[0]))
                        except ValueError:
                            pass
                    i = await self._query(r, w, "i", "i")
                    if i and i[0].replace(".", "", 1).isdigit():
                        self.st.tx_freq = int(float(i[0]))
                    v = await self._query(r, w, "v", "v")
                    if v and v[0] in ("VFOA", "VFOB", "Main", "Sub"):
                        self.st.vfo = "B" if v[0] in ("VFOB", "Sub") else "A"
                    self.st.link = True
                    self._wake.clear()
                    try:
                        await asyncio.wait_for(self._wake.wait(), self.poll)
                    except asyncio.TimeoutError:
                        pass
            except asyncio.CancelledError:
                raise
            except (OSError, ConnectionError, asyncio.TimeoutError, ValueError) as e:
                if self.st.link:
                    log.warning("rigctl %s:%s: %s", self.host, self.port, e)
                self.st.link = False
                self.st.smeter = -1
                if w:
                    w.close()
                await asyncio.sleep(2)


class RigctldProcess:
    """Arranca y vigila ``rigctld -m MODELO -r PUERTO -s BAUDIOS -T 127.0.0.1 -t 4536``."""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.proc = None
        self.last_error = ""

    def argv(self) -> list[str]:
        c = self.cfg
        exe = c.get("rigctld") or shutil.which("rigctld") or "rigctld"
        a = [exe, "-m", str(c["model"]), "-T", "127.0.0.1", "-t", str(c["internalPort"])]
        if int(c["model"]) not in (1, 2):              # 1 = Dummy, 2 = NET rigctl
            a += ["-r", c["device"], "-s", str(c["speed"])]
        if c.get("civ"):
            a += ["-c", str(c["civ"])]
        if c.get("conf"):
            a += ["-C", c["conf"]]
        return a

    async def run(self):
        backoff = 2
        while True:
            try:
                argv = self.argv()
                log.info("arrancando %s", " ".join(argv))
                self.proc = await asyncio.create_subprocess_exec(*argv, stdout=asyncio.subprocess.DEVNULL,
                                                                 stderr=asyncio.subprocess.PIPE)
                err = await self.proc.stderr.read()
                rc = await self.proc.wait()
                tail = err.decode(errors="replace").strip().splitlines()
                self.last_error = tail[-1] if tail else f"exit {rc}"
                log.warning("rigctld terminó (%s): %s", rc, self.last_error)
            except FileNotFoundError:
                self.last_error = "rigctld no instalado (apt install libhamlib-utils)"
                log.error(self.last_error)
            except asyncio.CancelledError:
                await self.stop()
                raise
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, 30)

    async def stop(self):
        if self.proc and self.proc.returncode is None:
            self.proc.terminate()
            try:
                await asyncio.wait_for(self.proc.wait(), 3)
            except asyncio.TimeoutError:
                self.proc.kill()


# ---------------------------------------------------------------------------------------------------------------------- SDRconnect
def ws_frame(opcode: int, payload: bytes) -> bytes:
    """Trama de cliente WebSocket (con máscara)."""
    mask = os.urandom(4)
    n = len(payload)
    h = bytes([0x80 | opcode])
    if n < 126:
        h += bytes([0x80 | n])
    elif n < 65536:
        h += bytes([0x80 | 126]) + n.to_bytes(2, "big")
    else:
        h += bytes([0x80 | 127]) + n.to_bytes(8, "big")
    return h + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


async def ws_read(r, max_bytes: int = 1 << 16) -> tuple[int, bytes]:
    h = await r.readexactly(2)
    op = h[0] & 0x0F
    n = h[1] & 0x7F
    if n == 126:
        n = int.from_bytes(await r.readexactly(2), "big")
    elif n == 127:
        n = int.from_bytes(await r.readexactly(8), "big")
    mask = await r.readexactly(4) if h[1] & 0x80 else None
    if n > max_bytes:                                   # espectro, audio: se descarta
        left = n
        while left:
            left -= len(await r.readexactly(min(left, 65536)))
        return op, b""
    p = await r.readexactly(n)
    if mask:
        p = bytes(b ^ mask[i % 4] for i, b in enumerate(p))
    return op, p


class SdrConnectRig(Rig):
    """SDRconnect de SDRplay: API WebSocket 1.0.x (JSON event_type / property / device / value). Solo recibe."""

    receiver = True
    READ = ("device_vfo_frequency", "demodulator", "device_center_frequency", "device_sample_rate", "signal_power")

    def __init__(self, host: str, port: int = 5454, name: str = ""):
        super().__init__()
        self.host, self.port, self.name = host, int(port), name
        self.center = 0
        self.sample_rate = 0.0
        self.dbm = -200.0
        self._w = None

    @staticmethod
    def msg(event: str, prop: str, value: str = "") -> str:
        return json.dumps({"event_type": event, "property": prop, "device": "primary", "value": value}, separators=(",", ":"))

    def _send(self, text: str):
        if self._w:
            self._w.write(ws_frame(1, text.encode()))

    def set_freq(self, hz: int) -> bool:
        if not self._w:
            return False
        hz = int(hz)
        if self.sample_rate > 0 and self.center > 0 and abs(hz - self.center) > self.sample_rate * 0.4:
            self._send(self.msg("set_property", "device_center_frequency", str(hz)))
            self.center = hz
        self._send(self.msg("set_property", "device_vfo_frequency", str(hz)))
        self.st.freq = hz
        return True

    def set_mode(self, mode: str) -> bool:
        m = norm_mode(mode)
        d = {"FM": "NFM", "PKTFM": "NFM", "WFM": "WFM", "AM": "AM", "USB": "USB", "PKTUSB": "USB",
             "LSB": "LSB", "PKTLSB": "LSB", "CW": "CW", "CWR": "CW"}.get(m)
        if not d or not self._w:
            return False
        self._send(self.msg("set_property", "demodulator", d))
        self.st.mode = m
        return True

    def on_text(self, text: str):
        try:
            d = json.loads(text)
        except ValueError:
            return
        prop, v = d.get("property") or "", d.get("value")
        if not prop:
            return
        v = "" if v is None else str(v)
        try:
            if prop == "device_vfo_frequency" and float(v) > 0:
                self.st.freq = int(round(float(v)))
            elif prop == "device_center_frequency" and float(v) > 0:
                self.center = int(round(float(v)))
            elif prop == "device_sample_rate":
                self.sample_rate = float(v)
            elif prop == "demodulator":
                self.st.mode = {"NFM": "FM", "SAM": "AM", "DSB": "AM"}.get(v, v)
            elif prop == "signal_power" and v:
                self.dbm = float(v)
                self.st.smeter = smeter_from_db(self.dbm + 73.0)
        except ValueError:
            pass

    async def run(self):
        while True:
            w = None
            try:
                r, w = await asyncio.wait_for(asyncio.open_connection(self.host, self.port), 3)
                key = base64.b64encode(os.urandom(16)).decode()
                w.write((f"GET / HTTP/1.1\r\nHost: {self.host}:{self.port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\nUser-Agent: APRS-LoRa-Plus-Rotor\r\n\r\n").encode())
                await w.drain()
                head = await asyncio.wait_for(r.readuntil(b"\r\n\r\n"), 5)
                if b" 101" not in head.split(b"\r\n", 1)[0]:
                    raise ConnectionError("no es un servidor WebSocket")
                self._w = w
                self.st.link = True
                log.info("SDRconnect conectado a %s:%s", self.host, self.port)
                for p in self.READ:
                    self._send(self.msg("get_property", p))
                poller = asyncio.create_task(self._poll())
                try:
                    while True:
                        op, p = await asyncio.wait_for(ws_read(r), 8)
                        if op == 1:
                            self.on_text(p.decode(errors="replace"))
                        elif op == 9:
                            w.write(ws_frame(10, p))
                        elif op == 8:
                            raise ConnectionError("SDRconnect cerró")
                finally:
                    poller.cancel()
            except asyncio.CancelledError:
                raise
            except (OSError, ConnectionError, asyncio.TimeoutError, asyncio.IncompleteReadError, asyncio.LimitOverrunError) as e:
                if self.st.link:
                    log.warning("SDRconnect: %s", e)
            self._w = None
            self.st.link = False
            self.st.smeter = -1
            if w:
                w.close()
            await asyncio.sleep(3)

    async def _poll(self):
        i = 0
        while True:
            await asyncio.sleep(0.5)
            self._send(self.msg("get_property", "signal_power"))
            i += 1
            if i % 4 == 0:
                self._send(self.msg("get_property", "device_vfo_frequency"))
                self._send(self.msg("get_property", "demodulator"))
            if self._w:
                await self._w.drain()


# ---------------------------------------------------------------------------------------------------------------------- seguimiento
class CatTracker:
    """Doppler: el cliente (app, tracker) manda bajada / subida nominales y la velocidad radial (km/s, + = se aleja)."""

    def __init__(self, min_step: int = 50, min_s: float = 0.5, timeout_s: float = 15.0):
        self.enabled = True
        self.active = False
        self.name = ""
        self.dl = self.ul = 0
        self.rr = 0.0
        self.upd = 0.0
        self.min_step, self.min_s, self.timeout_s = min_step, min_s, timeout_s
        self.doppler = 0
        self._sent_dl = self._sent_ul = 0
        self._last = 0.0

    def update(self, name: str, dl: int, ul: int, rr: float, now: float | None = None):
        self.name, self.dl, self.ul, self.rr = name, int(dl), int(ul), float(rr)
        self.upd = time.monotonic() if now is None else now
        self.active = True

    def reset(self):
        self._sent_dl = self._sent_ul = 0

    def step(self, rig: Rig, now: float | None = None):
        now = time.monotonic() if now is None else now
        if self.active and now - self.upd > self.timeout_s:
            self.active, self.doppler = False, 0
        if not self.active or not self.enabled or not rig.st.link or now - self._last < self.min_s:
            return
        if self.dl:
            f = doppler_rx(self.dl, self.rr)
            self.doppler = f - self.dl
            if not self._sent_dl or abs(f - self._sent_dl) >= self.min_step:
                rig.set_freq(f)
                self._sent_dl, self._last = f, now
        if self.ul:
            f = doppler_tx(self.ul, self.rr)
            if not self._sent_ul or abs(f - self._sent_ul) >= self.min_step:
                if rig.set_tx_freq(f):
                    self._sent_ul = f
                self._last = now


# ---------------------------------------------------------------------------------------------------------------------- servidor rigctld
DUMP_STATE = ("0\n2\n2\n"
              "100000.000000 3000000000.000000 0x1dff -1 -1 0x3 0x0\n0 0 0 0 0 0 0\n"
              "1800000.000000 3000000000.000000 0x1dbf 1000 100000 0x3 0x0\n0 0 0 0 0 0 0\n"
              "0x1dff 1\n0x1dff 10\n0x1dff 100\n0x1dff 1000\n0 0\n"
              "0x82 500\n0x82 200\n0x82 2000\n0x0c 2400\n0x0c 1800\n0x0c 3000\n0xc00 3000\n"
              "0x1021 15000\n0x1021 6000\n0x1021 10000\n0x40 230000\n0 0\n"
              "0\n0\n0\n0\n0\n0\n0\n0\n0x40000000\n0\n0\n0\n")

SHORT = {"f": "get_freq", "F": "set_freq", "m": "get_mode", "M": "set_mode", "v": "get_vfo", "V": "set_vfo",
         "t": "get_ptt", "T": "set_ptt", "s": "get_split_vfo", "S": "set_split_vfo", "i": "get_split_freq",
         "I": "set_split_freq", "x": "get_split_mode", "X": "set_split_mode", "l": "get_level", "L": "set_level",
         "_": "get_info", "1": "dump_caps", "q": "quit", "Q": "quit"}


class RigctlSession:
    """Servidor del protocolo de rigctld (Hamlib NET rigctl), como rc_rigctld.h."""

    def __init__(self):
        self.buf = b""

    def feed(self, data: bytes, rig: Rig) -> tuple[bytes, bool]:
        self.buf += data
        out, close = [], False
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            o, c = self.handle(line.decode(errors="replace").strip("\r").strip(), rig)
            out.append(o)
            close = close or c
            if c:
                break
        if len(self.buf) > 256:
            self.buf = b""
        return "".join(out).encode(), close

    @staticmethod
    def _rprt(code: int) -> str:
        return f"RPRT {-code if code else 0}\n"

    def handle(self, raw: str, rig: Rig) -> tuple[str, bool]:
        if not raw:
            return "", False
        if raw[0] in "+;|,":
            return self._rprt(4), False
        if raw[0] == "\\":
            t = raw[1:].split()
            if not t:
                return "", False
            cmd, t = t[0], t[1:]
        else:
            cmd = SHORT.get(raw[0])
            t = raw[1:].split()
            if not cmd:
                return self._rprt(4), False
        st = rig.st

        def num(s):
            try:
                v = float(s)
                return int(round(v)) if 0 < v < 1e11 else None
            except ValueError:
                return None
        if cmd == "get_freq":
            return f"{st.freq}\n", False
        if cmd == "set_freq":
            f = num(t[-1]) if t else None
            return (self._rprt(1) if f is None else self._rprt(0 if rig.set_freq(f) else 9)), False
        if cmd == "get_mode":
            m = st.mode or "USB"
            return f"{m}\n{passband(m)}\n", False
        if cmd == "set_mode":
            if not t:
                return self._rprt(1), False
            if t[0] == "?":
                return "LSB USB CW CWR AM FM WFM RTTY RTTYR PKTLSB PKTUSB PKTFM\n", False
            return self._rprt(0 if rig.set_mode(t[0]) else 1), False
        if cmd == "get_vfo":
            return ("VFOB\n" if st.vfo == "B" else "VFOA\n"), False
        if cmd == "set_vfo":
            if not t:
                return self._rprt(1), False
            if t[0] == "?":
                return "VFOA VFOB Main Sub currVFO\n", False
            want = {"VFOA": "A", "Main": "A", "MAIN": "A", "VFOB": "B", "Sub": "B", "SUB": "B"}.get(t[0])
            if want is None and t[0] not in ("currVFO", "VFO"):
                return self._rprt(1), False
            return self._rprt(0 if rig.set_vfo(want or st.vfo) else 9), False
        if cmd == "get_ptt":
            return ("1\n" if st.ptt else "0\n"), False
        if cmd == "set_ptt":
            try:
                on = float(t[-1]) != 0
            except (ValueError, IndexError):
                return self._rprt(1), False
            return self._rprt(0 if rig.set_ptt(on) else 9), False
        if cmd == "get_split_vfo":
            return ("1\n" if st.split else "0\n") + ("VFOA\n" if st.vfo == "B" else "VFOB\n"), False
        if cmd == "set_split_vfo":
            try:
                on = float(t[0]) != 0
            except (ValueError, IndexError):
                return self._rprt(1), False
            return self._rprt(0 if rig.set_split(on) else 9), False
        if cmd == "get_split_freq":
            return f"{st.tx_freq}\n", False
        if cmd == "set_split_freq":
            f = num(t[-1]) if t else None
            return (self._rprt(1) if f is None else self._rprt(0 if rig.set_tx_freq(f) else 9)), False
        if cmd == "get_split_mode":
            m = st.tx_mode or st.mode or "USB"
            return f"{m}\n{passband(m)}\n", False
        if cmd == "set_split_mode":
            return self._rprt(0), False
        if cmd == "get_level":
            if t and t[-1] == "STRENGTH":
                return f"{smeter_db(st.smeter)}\n", False
            if t and t[-1] == "RAWSTR":
                return f"{max(0, st.smeter) * 16}\n", False
            if t and t[0] == "?":
                return "STRENGTH RAWSTR\n", False
            return self._rprt(4), False
        if cmd == "get_info":
            return f"{rig.info()}\n", False
        if cmd == "get_powerstat":
            return "1\n", False
        if cmd == "chk_vfo":
            return "0\n", False
        if cmd == "dump_state":
            return DUMP_STATE, False
        if cmd == "dump_caps":
            return "Model name:\tAPRS LoRa+ Rotor CAT\nMfg name:\tEA7JRS\n", False
        if cmd == "quit":
            return self._rprt(0), True
        return self._rprt(4), False


# ---------------------------------------------------------------------------------------------------------------------- conjunto
class Cat:
    """Lo que arranca el demonio según ``cat.type``."""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.proc: RigctldProcess | None = None
        self.rig: Rig | None = None
        self.tracker = CatTracker(int(cfg.get("trackStep", 50)))
        self._ptt_since = 0.0
        t = cfg.get("type", "none")
        model = cfg.get("name") or ""
        if t == "hamlib":
            self.proc = RigctldProcess(cfg)
            self.rig = RigctlClientRig("127.0.0.1", int(cfg["internalPort"]),
                                       f"{model or 'Hamlib ' + str(cfg['model'])} · Hamlib {cfg['model']} · {cfg['speed']}",
                                       int(cfg.get("pollMs", 300)))
        elif t == "rigctl":
            port = int(cfg.get("port") or 4532)
            self.rig = RigctlClientRig(cfg["host"], port, f"{model or 'SDR'} · rigctl {cfg['host']}:{port}", int(cfg.get("pollMs", 300)))
        elif t == "sdrconnect":
            port = int(cfg.get("port") or 5454)
            self.rig = SdrConnectRig(cfg["host"], port, f"{model or 'SDRplay'} · SDRconnect {cfg['host']}:{port}")
        if self.rig:
            self.rig.allow_ptt = bool(cfg.get("ptt")) and not self.rig.receiver

    def ptt_allowed(self) -> bool:
        return bool(self.rig and self.rig.allow_ptt)

    async def run(self):
        if not self.rig:
            return
        if self.proc:
            asyncio.create_task(self.proc.run())
            await asyncio.sleep(1)
        asyncio.create_task(self.rig.run())
        max_s = float(self.cfg.get("pttMaxS", 180))
        while True:
            await asyncio.sleep(0.25)
            self.tracker.step(self.rig)
            if self.rig.st.ptt:
                if not self._ptt_since:
                    self._ptt_since = time.monotonic()
                elif time.monotonic() - self._ptt_since > max_s:
                    log.warning("PTT soltado por tiempo")
                    self.rig.set_ptt(False)
                    self._ptt_since = 0
            else:
                self._ptt_since = 0

    def event(self) -> dict:
        r, s, t = self.rig, self.rig.st, self.tracker
        d = {"e": "cat", "rig": r.info(), "l": s.link, "f": s.freq, "m": s.mode, "sp": s.split, "v": s.vfo,
             "ptt": s.ptt, "sm": s.smeter, "pa": self.ptt_allowed(),
             "tr": {"en": t.enabled, "on": t.active, "n": t.name, "dop": t.doppler}}
        if s.tx_freq and not r.receiver:
            d["tf"] = s.tx_freq
        return d

    async def rigctld_client(self, reader, writer):
        s = RigctlSession()
        try:
            while data := await reader.read(256):
                out, close = s.feed(data, self.rig)
                if out:
                    writer.write(out)
                    await writer.drain()
                if close:
                    break
        except ConnectionError:
            pass
        finally:
            writer.close()
