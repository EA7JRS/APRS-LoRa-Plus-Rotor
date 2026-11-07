# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""KISS y AX.25 UI <-> TNC2, para hablar con un TNC: un tracker / iGate APRS LoRa+ en modo KISS (TCP 8001 o BLE/USB),
Direwolf, o cualquier TNC KISS por TCP o puerto serie."""

from __future__ import annotations

FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD


def kiss_encode(payload: bytes, port: int = 0) -> bytes:
    out = bytearray([FEND, (port & 0x0F) << 4])
    for b in payload:
        if b == FEND:
            out += bytes([FESC, TFEND])
        elif b == FESC:
            out += bytes([FESC, TFESC])
        else:
            out.append(b)
    out.append(FEND)
    return bytes(out)


class KissDecoder:
    def __init__(self):
        self.buf = bytearray()
        self.esc = False
        self.inframe = False

    def feed(self, data: bytes):
        """Devuelve las tramas de datos (sin el byte de orden) completas."""
        frames = []
        for b in data:
            if b == FEND:
                if self.inframe and len(self.buf) > 1 and (self.buf[0] & 0x0F) == 0:
                    frames.append(bytes(self.buf[1:]))
                self.buf.clear()
                self.inframe = True
                self.esc = False
                continue
            if not self.inframe:
                continue
            if self.esc:
                self.buf.append(FEND if b == TFEND else FESC if b == TFESC else b)
                self.esc = False
            elif b == FESC:
                self.esc = True
            else:
                if len(self.buf) < 1024:
                    self.buf.append(b)
        return frames


def _addr_encode(call: str, last: bool, hbit: bool = False) -> bytes:
    call = call.upper()
    base, _, ssid = call.partition("-")
    base = base.rstrip("*")
    try:
        n = int(ssid.rstrip("*")) if ssid else 0
    except ValueError:
        n = 0
    if not base or len(base) > 6 or not 0 <= n <= 15:
        raise ValueError("indicativo AX.25 no valido: " + call)
    b = bytearray((ord(c) << 1) & 0xFE for c in f"{base:<6}")
    b.append(0x60 | (n << 1) | (0x80 if hbit else 0) | (1 if last else 0))
    return bytes(b)


def _addr_decode(b: bytes) -> tuple[str, bool, bool]:
    call = "".join(chr(x >> 1) for x in b[:6]).strip()
    ssid = (b[6] >> 1) & 0x0F
    return (call + (f"-{ssid}" if ssid else "")), bool(b[6] & 0x80), bool(b[6] & 1)


def tnc2_to_ax25(frame: str) -> bytes:
    hdr, _, info = frame.partition(":")
    src, _, rest = hdr.partition(">")
    parts = rest.split(",")
    dest, path = parts[0], [p for p in parts[1:] if p]
    out = bytearray(_addr_encode(dest, False, True) + _addr_encode(src, not path))
    for i, p in enumerate(path):
        out += _addr_encode(p, i == len(path) - 1, p.endswith("*"))
    out += bytes([0x03, 0xF0])
    out += info.encode("latin-1", "replace")
    return bytes(out)


def ax25_to_tnc2(b: bytes) -> str | None:
    if len(b) < 16:
        return None
    addrs = []
    i = 0
    while i + 7 <= len(b):
        call, h, last = _addr_decode(b[i:i + 7])
        addrs.append((call, h))
        i += 7
        if last:
            break
    else:
        return None
    if len(addrs) < 2 or i + 2 > len(b) or b[i] != 0x03 or b[i + 1] != 0xF0:
        return None
    dest, src = addrs[0][0], addrs[1][0]
    path = [c + ("*" if h else "") for c, h in addrs[2:]]
    info = b[i + 2:].decode("latin-1")
    return f"{src}>{dest}" + "".join("," + p for p in path) + ":" + info
