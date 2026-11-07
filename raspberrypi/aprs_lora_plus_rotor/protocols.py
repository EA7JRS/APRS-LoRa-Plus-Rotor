# Copyright (C) 2026 Manuel - EA7JRS
#
# This file is part of APRS LoRa+ Rotor.
#
# APRS LoRa+ Rotor is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# APRS LoRa+ Rotor is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with APRS LoRa+ Rotor. If not, see <https://www.gnu.org/licenses/>.
"""Protocolos de rotor del lado servidor (igual que firmware/lib/RotorCore): rotctld, GS-232A/B, EasyComm II y Rot2Prog.

El objeto ``rot`` es cualquier cosa con la interfaz de :class:`Rotor` (en el demonio, el cliente del rotctld de Hamlib).
"""

from __future__ import annotations

import math

# Banderas de estado (rot_status_t de Hamlib)
ST_BUSY, ST_MOVING, ST_MOVING_AZ, ST_MOVING_LEFT, ST_MOVING_RIGHT = 1, 2, 4, 8, 16
ST_MOVING_EL, ST_MOVING_UP, ST_MOVING_DOWN = 32, 64, 128
ST_LIMIT_UP, ST_LIMIT_DOWN, ST_LIMIT_LEFT, ST_LIMIT_RIGHT = 256, 512, 1024, 2048
ST_OVERLAP_UP, ST_OVERLAP_DOWN, ST_OVERLAP_LEFT, ST_OVERLAP_RIGHT = 4096, 8192, 16384, 65536

MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT = 2, 4, 8, 16

E_OK, E_INVAL, E_NIMPL, E_IO, E_PROTO, E_REJECTED = 0, 1, 4, 6, 8, 9

STATUS_NAMES = [
    (ST_BUSY, "BUSY"), (ST_MOVING, "MOVING"), (ST_MOVING_AZ, "MOVING_AZ"), (ST_MOVING_LEFT, "MOVING_LEFT"),
    (ST_MOVING_RIGHT, "MOVING_RIGHT"), (ST_MOVING_EL, "MOVING_EL"), (ST_MOVING_UP, "MOVING_UP"),
    (ST_MOVING_DOWN, "MOVING_DOWN"), (ST_LIMIT_UP, "LIMIT_UP"), (ST_LIMIT_DOWN, "LIMIT_DOWN"),
    (ST_LIMIT_LEFT, "LIMIT_LEFT"), (ST_LIMIT_RIGHT, "LIMIT_RIGHT"), (ST_OVERLAP_UP, "OVERLAP_UP"),
    (ST_OVERLAP_DOWN, "OVERLAP_DOWN"), (ST_OVERLAP_LEFT, "OVERLAP_LEFT"), (ST_OVERLAP_RIGHT, "OVERLAP_RIGHT"),
]


def status_text(st: int) -> str:
    return "".join(n + " " for f, n in STATUS_NAMES if st & f)


def wrap360(a: float) -> float:
    return a % 360.0


def angle_diff(a: float, b: float) -> float:
    d = (b - a) % 360.0
    return d - 360.0 if d > 180.0 else d


class Rotor:
    """Interfaz comun (la misma que RC::Rotor del firmware)."""

    min_az, max_az, min_el, max_el, has_el = 0.0, 360.0, 0.0, 90.0, True

    def set_pos(self, az: float, el: float) -> bool: raise NotImplementedError
    def set_az(self, az: float) -> bool: return self.set_pos(az, self.get_pos()[1])
    def get_pos(self) -> tuple[float, float]: raise NotImplementedError
    def stop(self) -> bool: raise NotImplementedError
    def stop_az(self) -> bool: return self.stop()
    def stop_el(self) -> bool: return self.stop()
    def park(self) -> bool: raise NotImplementedError
    def move(self, direction: int, speed: int) -> bool: raise NotImplementedError
    def status(self) -> int: return 0
    def info(self) -> str: return "APRS LoRa+ Rotor"


def _num(s: str) -> float | None:
    try:
        v = float(s.replace(",", ".")) if isinstance(s, str) else float(s)
    except (TypeError, ValueError):
        return None
    return v if math.isfinite(v) else None


def parse_move_dir(t) -> int:
    v = _num(str(t))
    if v is not None:
        return int(v)
    u = str(t).upper()
    return {"UP": MOVE_UP, "DOWN": MOVE_DOWN, "LEFT": MOVE_LEFT, "CCW": MOVE_LEFT, "RIGHT": MOVE_RIGHT, "CW": MOVE_RIGHT}.get(u, 0)


class LineBuffer:
    def __init__(self, seps=b"\n", limit=160, upper=False):
        self.buf = bytearray()
        self.seps = seps
        self.limit = limit
        self.upper = upper

    def feed(self, data: bytes):
        for b in data:
            if b in self.seps:
                if self.buf:
                    line = self.buf.decode("latin-1")
                    self.buf.clear()
                    yield line.upper() if self.upper else line
            elif b != 0x0D or 0x0D in self.seps:
                if len(self.buf) < self.limit:
                    self.buf.append(b)
                else:
                    self.buf.clear()


# ------------------------------------------------------------------------------------------------------------------------
class RotctlSession:
    """Servidor del protocolo de red de rotctld (Hamlib 4.x). feed() devuelve (respuesta, cerrar)."""

    def __init__(self, model: int = 2):
        self.model = model
        self.lines = LineBuffer(b"\n", 160)

    def feed(self, data: bytes, rot: Rotor) -> tuple[bytes, bool]:
        out, close = [], False
        for line in self.lines.feed(data):
            r, c = self.handle(line.replace("\r", ""), rot)
            out.append(r)
            close = close or c
        return "".join(out).encode(), close

    @staticmethod
    def _rprt(code: int) -> str:
        return f"RPRT {0 if code == 0 else -code}\n"

    def handle(self, raw: str, rot: Rotor) -> tuple[str, bool]:
        raw = raw.lstrip()
        if not raw:
            return "", False
        if raw[0] in "+;|,":
            return self._rprt(E_NIMPL), False
        if raw[0] == "\\":
            t = raw[1:].split()
            if not t:
                return "", False
            cmd, t = t[0], t[1:]
        else:
            t = raw[1:].split()
            cmd = {"P": "set_pos", "p": "get_pos", "S": "stop", "K": "park", "R": "reset", "M": "move", "s": "get_status",
                   "_": "get_info", "1": "dump_caps", "q": "quit", "Q": "quit"}.get(raw[0])
            if cmd is None:
                return self._rprt(E_NIMPL), False
        if cmd == "set_pos":
            if len(t) < 2:
                return self._rprt(E_INVAL), False
            az, el = _num(t[0]), _num(t[1])
            if az is None or el is None or not (-360 <= az <= 720) or not (-90 <= el <= 180):
                return self._rprt(E_INVAL), False
            if az < rot.min_az or az > rot.max_az:
                az = wrap360(az)
            el = min(max(el, rot.min_el), rot.max_el)
            return self._rprt(E_OK if rot.set_pos(az, el) else E_REJECTED), False
        if cmd == "get_pos":
            az, el = rot.get_pos()
            return f"{az:.6f}\n{el:.6f}\n", False
        if cmd in ("stop", "reset"):
            return self._rprt(E_OK if rot.stop() else E_REJECTED), False
        if cmd == "park":
            return self._rprt(E_OK if rot.park() else E_REJECTED), False
        if cmd == "move":
            if not t:
                return self._rprt(E_INVAL), False
            d = parse_move_dir(t[0])
            sp = _num(t[1]) if len(t) > 1 else 0.0
            if sp is None or d not in (MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT):
                return self._rprt(E_INVAL), False
            speed = 0 if sp < 0 else min(int(sp), 100)
            return self._rprt(E_OK if rot.move(d, speed) else E_REJECTED), False
        if cmd == "get_status":
            return status_text(rot.status()) + "\n", False
        if cmd == "get_info":
            return rot.info() + "\n", False
        if cmd == "dump_state":
            return (f"1\n{self.model}\nmin_az={rot.min_az:.6f}\nmax_az={rot.max_az:.6f}\nmin_el={rot.min_el:.6f}\n"
                    f"max_el={rot.max_el:.6f}\nsouth_zero=0\nrot_type={'AzEl' if rot.has_el else 'Az'}\ndone\n"), False
        if cmd == "dump_caps":
            return (f"Model name:\tAPRS LoRa+ Rotor\nMfg name:\tEA7JRS\nMin Azimuth:\t{rot.min_az:.2f}\nMax Azimuth:\t{rot.max_az:.2f}\n"
                    f"Min Elevation:\t{rot.min_el:.2f}\nMax Elevation:\t{rot.max_el:.2f}\n"), False
        if cmd == "chk_vfo":
            return "0\n", False
        if cmd == "quit":
            return self._rprt(E_OK), True
        return self._rprt(E_NIMPL), False


# ------------------------------------------------------------------------------------------------------------------------
def gs232_pos_reply(b: bool, az: float, el: float, az_only=False, el_only=False) -> str:
    a = min(max(round(az), 0), 999)
    e = min(max(round(el), 0), 999)
    if b:
        return f"AZ={a:03d}\r\n" if az_only else f"EL={e:03d}\r\n" if el_only else f"AZ={a:03d}  EL={e:03d}\r\n"
    return f"+0{a:03d}\r\n" if az_only else f"+0{e:03d}\r\n" if el_only else f"+0{a:03d}+0{e:03d}\r\n"


class GS232Server:
    def __init__(self, mode_b: bool = True):
        self.b = mode_b
        self.speed = 4
        self.lines = LineBuffer(b"\r\n", 32, upper=True)

    def feed(self, data: bytes, rot: Rotor) -> bytes:
        return "".join(self.handle(line, rot) for line in self.lines.feed(data)).encode()

    def handle(self, l: str, rot: Rotor) -> str:
        bad = "?>\r\n"
        c = l[0]
        if c == "C":
            az, el = rot.get_pos()
            if l == "C":
                return gs232_pos_reply(self.b, az, el, True)
            if l == "C2":
                return gs232_pos_reply(self.b, az, el, not rot.has_el)
            return bad
        if l == "B":
            az, el = rot.get_pos()
            return gs232_pos_reply(self.b, az, el, False, True)
        if c == "W":
            p = l[1:].split()
            try:
                a, e = int(p[0]), int(p[1])
            except (IndexError, ValueError):
                return bad
            if not (0 <= a <= 450 and 0 <= e <= 180):
                return bad
            return "" if rot.set_pos(a, e) else bad
        if c == "M":
            try:
                a = int(l[1:])
            except ValueError:
                return bad
            if not 0 <= a <= 450:
                return bad
            return "" if rot.set_az(a) else bad
        simple = {"S": rot.stop, "A": rot.stop_az, "E": rot.stop_el}
        if l in simple:
            simple[l]()
            return ""
        moves = {"L": MOVE_LEFT, "R": MOVE_RIGHT, "U": MOVE_UP, "D": MOVE_DOWN}
        if l in moves:
            rot.move(moves[l], self.speed * 25)
            return ""
        if c == "X" and len(l) == 2 and l[1] in "1234":
            self.speed = int(l[1])
            return ""
        if l in ("P36", "P45", "Z"):
            return ""
        if c == "H":
            return "APRS LoRa+ Rotor GS-232: C C2 B Waaa eee Maaa S A E L R U D X1-4\r\n"
        return bad


class EasyCommServer:
    def __init__(self):
        self.lines = LineBuffer(b"\r\n", 96, upper=True)

    def feed(self, data: bytes, rot: Rotor) -> bytes:
        return "".join(self.handle(line, rot) for line in self.lines.feed(data)).encode()

    def handle(self, l: str, rot: Rotor) -> str:
        out = ""
        n_az = n_el = None
        ask_az = ask_el = False
        for w in l.split():
            if w.startswith("AZ"):
                if w == "AZ":
                    ask_az = True
                else:
                    n_az = _num(w[2:])
            elif w.startswith("EL"):
                if w == "EL":
                    ask_el = True
                else:
                    n_el = _num(w[2:])
            elif w == "SA":
                rot.stop_az()
            elif w == "SE":
                rot.stop_el()
            elif w in ("ML", "MR", "MU", "MD"):
                rot.move({"ML": MOVE_LEFT, "MR": MOVE_RIGHT, "MU": MOVE_UP, "MD": MOVE_DOWN}[w], 0)
            elif w == "PARK":
                rot.park()
            elif w == "RESET":
                rot.stop()
            elif w == "VE":
                out += "VEAPRS LoRa+ Rotor\n"
            elif w == "GS":
                out += f"GS{2 if rot.status() & ST_MOVING else 1}\n"
        if n_az is not None or n_el is not None:
            az, el = rot.get_pos()
            if n_el is not None:
                rot.set_pos(n_az if n_az is not None else az, n_el)
            else:
                rot.set_az(n_az)
        if ask_az or ask_el:
            az, el = rot.get_pos()
            if ask_az and ask_el:
                out += f"AZ{az:.1f} EL{el:.1f}\n"
            elif ask_az:
                out += f"AZ{az:.1f}\n"
            else:
                out += f"EL{el:.1f}\n"
        return out


def rot2prog_reply(az: float, el: float) -> bytes:
    ua = max(round((360.0 + az) * 10), 0)
    ue = max(round((360.0 + el) * 10), 0)
    return bytes([0x57, ua // 1000 % 10, ua // 100 % 10, ua // 10 % 10, ua % 10, 10,
                  ue // 1000 % 10, ue // 100 % 10, ue // 10 % 10, ue % 10, 10, 0x20])


def rot2prog_parse_reply(r: bytes) -> tuple[float, float] | None:
    if len(r) != 12 or r[0] != 0x57 or r[11] != 0x20 or any(r[i] > 9 for i in (1, 2, 3, 4, 6, 7, 8, 9)):
        return None
    return (r[1] * 100 + r[2] * 10 + r[3] + r[4] / 10 - 360, r[6] * 100 + r[7] * 10 + r[8] + r[9] / 10 - 360)


def rot2prog_set_frame(az: float, el: float, ph: int = 10, pv: int = 10) -> bytes:
    ua, ue = round(ph * (360.0 + az)), round(pv * (360.0 + el))
    return bytes([0x57]) + f"{ua % 10000:04d}".encode() + bytes([ph]) + f"{ue % 10000:04d}".encode() + bytes([pv, 0x2F, 0x20])


class Rot2ProgServer:
    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes, rot: Rotor) -> bytes:
        out = bytearray()
        for b in data:
            if not self.buf and b != 0x57:
                continue
            self.buf.append(b)
            if len(self.buf) == 13:
                f, self.buf = bytes(self.buf), bytearray()
                if f[12] != 0x20:
                    continue
                k = f[11]
                if k == 0x0F:
                    rot.stop()
                elif k == 0x2F:
                    try:
                        ua, ue = int(f[1:5].decode()), int(f[6:10].decode())
                    except ValueError:
                        ua = ue = None
                    if ua is not None:
                        ph, pv = f[5] or 10, f[10] or 10
                        az, el = ua / ph - 360.0, ue / pv - 360.0
                        rot.set_pos(az + 360.0 if az < 0 else az, max(el, 0.0))
                elif k != 0x1F:
                    continue
                az, el = rot.get_pos()
                out += rot2prog_reply(az, el)
        return bytes(out)


def make_server(proto: int):
    """0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog, 4 rotctld (devuelve un objeto con feed(data, rot) -> bytes)."""
    if proto in (0, 1):
        return GS232Server(proto == 1)
    if proto == 2:
        return EasyCommServer()
    if proto == 3:
        return Rot2ProgServer()

    class _R(RotctlSession):
        def feed(self, data, rot):  # type: ignore[override]
            return RotctlSession.feed(self, data, rot)[0]
    return _R()
