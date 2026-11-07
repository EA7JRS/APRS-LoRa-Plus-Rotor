# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Control del rotor por mensajes APRS (igual que rc_aprs.h del firmware). Ver docs/APRS_CONTROL.md."""

from __future__ import annotations

import base64
import hashlib
import hmac
import struct
import time
from dataclasses import dataclass, field

from .protocols import Rotor, ST_MOVING, wrap360

LORA_HEADER = b"<\xff\x01"


def hotp(key: bytes, counter: int, digits: int = 6) -> int:
    h = hmac.new(key, struct.pack(">Q", counter), hashlib.sha1).digest()
    o = h[19] & 0x0F
    return (struct.unpack(">I", h[o:o + 4])[0] & 0x7FFFFFFF) % (10 ** digits)


def b32key(secret: str) -> bytes:
    s = secret.upper().replace(" ", "").replace("-", "").rstrip("=")
    return base64.b32decode(s + "=" * (-len(s) % 8))


def base_call(c: str) -> str:
    return c.split("-")[0].upper()


@dataclass
class Message:
    src: str = ""
    dest: str = ""
    path: str = ""
    addressee: str = ""
    text: str = ""
    id: str = ""


def parse_message(frame: str) -> Message | None:
    if frame.startswith("<\xff\x01"):
        frame = frame[3:]
    gt, colon = frame.find(">"), frame.find(":")
    if gt <= 0 or colon < 0 or gt > colon:
        return None
    m = Message()
    m.src = frame[:gt].upper()
    hdr = frame[gt + 1:colon]
    m.dest, _, m.path = hdr.partition(",")
    info = frame[colon + 1:]
    if info.startswith("}"):
        return parse_message(info[1:])
    if len(info) < 11 or info[0] != ":" or info[10] != ":":
        return None
    m.addressee = info[1:10].strip().upper()
    body = info[11:].rstrip("\r\n")
    br = body.rfind("{")
    if br >= 0:
        mid = body[br + 1:].split("}")[0]
        if 1 <= len(mid) <= 5:
            m.id, body = mid, body[:br]
    m.text = body
    return m


def build_message(src: str, tocall: str, path: str, to: str, text: str, mid: str = "") -> str:
    f = f"{src}>{tocall}" + (f",{path}" if path else "") + f"::{to[:9]:<9}:{text[:67]}"
    return f + (f"{{{mid}" if mid else "")


def status_line(rot: Rotor, tail: str) -> str:
    az, el = rot.get_pos()
    return f"AZ {az:.0f} EL {el:.0f} {tail}" if rot.has_el else f"AZ {az:.0f} {tail}"


@dataclass
class AprsConfig:
    callsign: str = "N0CALL-12"
    tocall: str = "APZLRR"
    path: str = ""
    allow: str = ""
    totp: str = ""
    reply: bool = True


@dataclass
class AprsControl:
    cfg: AprsConfig = field(default_factory=AprsConfig)
    last_result: str = ""
    _seen: list = field(default_factory=list)
    _last_counter: int = 0

    def allowed(self, src: str) -> bool:
        s = src.upper()
        lst = (self.cfg.allow or base_call(self.cfg.callsign)).upper().replace(",", " ").replace(";", " ").split()
        return any((base_call(s) == t) if "-" not in t else (s == t) for t in lst)

    def check_code(self, code: str, utc: float) -> bool:
        if utc < 1600000000 or len(code) != 6 or not code.isdigit():
            return False
        try:
            key = b32key(self.cfg.totp)
        except Exception:
            return False
        step = int(utc) // 30
        for c in (step - 1, step, step + 1):
            if c > self._last_counter and hotp(key, c) == int(code):
                self._last_counter = c
                return True
        return False

    def on_frame(self, frame: str, now: float | None = None, utc: float | None = None, rot: Rotor | None = None) -> tuple[bool, list[str]]:
        """Devuelve (era para el rotor, tramas TNC2 a enviar)."""
        now = time.monotonic() if now is None else now
        utc = time.time() if utc is None else utc
        m = parse_message(frame)
        tx: list[str] = []
        me = self.cfg.callsign.upper()
        if not m or m.addressee != me:
            return False, tx
        if m.text.startswith(("ack", "rej")):
            return True, tx
        msg = lambda text, mid="": build_message(me, self.cfg.tocall, self.cfg.path, m.src, text, mid)  # noqa: E731
        self._seen = [s for s in self._seen if now - s[2] < 180]
        if m.id and any(s[0] == m.src and s[1] == m.id for s in self._seen):
            return True, [msg("ack" + m.id)]
        if not self.allowed(m.src):
            self.last_result = "denied " + m.src
            return True, [msg("rej" + m.id)] if m.id else []
        t = m.text.strip().upper().split()
        if not t:
            return True, tx
        if self.cfg.totp:
            if not self.check_code(t[-1], utc):
                self.last_result = f"bad code {m.src}" if utc else "no time for code"
                if m.id:
                    tx.append(msg("rej" + m.id))
                if self.cfg.reply:
                    tx.append(msg("ERR code" if utc else "ERR no time"))
                return True, tx
            t = t[:-1]
        if m.id:
            self._seen.append((m.src, m.id, now))
            self._seen = self._seen[-8:]
            tx.append(msg("ack" + m.id))
        res = self.execute(t, rot) if t else "ERR cmd (HELP)"
        self.last_result = f"{m.src}: {m.text.strip()} -> {res}"
        if self.cfg.reply:
            tx.append(msg(res))
        return True, tx

    @staticmethod
    def _num(s: str):
        try:
            return float(s.replace(",", "."))
        except ValueError:
            return None

    def execute(self, t: list[str], rot: Rotor) -> str:
        c = t[0]
        if c in ("?", "POS", "STATUS", "Q"):
            return status_line(rot, "MOVING" if rot.status() & ST_MOVING else "OK")
        if c in ("STOP", "S"):
            rot.stop()
            return status_line(rot, "STOP")
        if c in ("PARK", "K"):
            return status_line(rot, "PARK") if rot.park() else "ERR park"
        if c in ("HELP", "H"):
            return "P az el|AZ az EL el|PARK|STOP|?"
        az = el = None
        has_el = False
        if c == "P" and len(t) >= 2:
            az = self._num(t[1])
            if len(t) >= 3:
                el, has_el = self._num(t[2]), True
                if el is None:
                    return "ERR el"
        elif len(c) > 1 and c[0] == "P" and self._num(c[1:]) is not None:
            az = self._num(c[1:])
            if len(t) >= 2:
                el, has_el = self._num(t[1]), True
                if el is None:
                    return "ERR el"
        elif c == "AZ" and len(t) >= 2:
            az = self._num(t[1])
            if len(t) >= 4 and t[2] == "EL":
                el, has_el = self._num(t[3]), True
                if el is None:
                    return "ERR el"
        if az is None:
            return "ERR cmd (HELP)"
        el = el or 0.0
        if not (-360 <= az <= 720) or not (-10 <= el <= 180):
            return "ERR range"
        if has_el and rot.has_el:
            el = min(max(el, rot.min_el), rot.max_el)
            ok = rot.set_pos(az, el)
        else:
            ok = rot.set_az(az)
        if not ok:
            return "ERR rejected"
        return f"GO AZ {wrap360(az):.0f} EL {el:.0f}" if has_el and rot.has_el else f"GO AZ {wrap360(az):.0f}"
