# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Protocolo de control JSON por lineas (docs/CONTROL_PROTOCOL.md), el mismo que rc_api.h del firmware."""

from __future__ import annotations

import json
import time

from .protocols import ST_BUSY, ST_MOVING, parse_move_dir

API_VERSION = 1
CFG_CHUNK = 180


def dumps(d: dict) -> str:
    return json.dumps(d, separators=(",", ":"), ensure_ascii=False)


class ApiHost:
    """Lo implementa el demonio (main.Daemon)."""

    rotor = None

    def info(self) -> dict: return {}
    def pos_extra(self) -> dict: return {}
    def config_json(self) -> str: return "{}"
    def apply_config(self, text: str) -> str: return "not supported"
    def raw(self) -> dict: return {}
    def clear_fault(self): pass
    def reboot(self): pass
    def pin(self) -> str: return ""


def pos_event(h: ApiHost) -> str:
    r = h.rotor
    az, el = r.get_pos()
    st = r.status()
    d = {"e": "pos", "az": round(az, 1)}
    if r.has_el:
        d["el"] = round(el, 1)
    d["st"] = st
    d["mv"] = bool(st & ST_MOVING)
    d.update(h.pos_extra())
    return dumps(d)


class ApiSession:
    def __init__(self):
        self.authed = False
        self.sub_ms = 0
        self._cfg_in = ""
        self._cfg_next = 0
        self._last_evt = 0.0
        self._was_moving = False

    def handle(self, line: str, h: ApiHost) -> list[str]:
        out: list[str] = []
        ok = lambda c, **k: out.append(dumps({"e": "ok", "c": c, **k}))  # noqa: E731
        err = lambda c, m: out.append(dumps({"e": "err", **({"c": c} if c else {}), "m": m}))  # noqa: E731
        try:
            d = json.loads(line)
            c = d["c"]
            if not isinstance(c, str):
                raise ValueError
        except (ValueError, KeyError, TypeError):
            err("", "bad json")
            return out
        need_pin = bool(h.pin())
        if c == "auth":
            if not need_pin or d.get("pin") == h.pin():
                self.authed = True
                ok(c)
            else:
                self.authed = False
                err(c, "bad pin")
            return out
        if c == "info":
            r = h.rotor
            i = {"e": "info", "api": API_VERSION,
                 "lim": {"minAz": r.min_az, "maxAz": r.max_az, "minEl": r.min_el, "maxEl": r.max_el},
                 "hasEl": r.has_el, "pin": need_pin}
            i.update(h.info())
            out.append(dumps(i))
            return out
        if c == "get":
            out.append(pos_event(h))
            return out
        if c == "raw":
            out.append(dumps({"e": "raw", **h.raw()}))
            return out
        if c == "sub":
            ms = int(d.get("ms", 1000))
            self.sub_ms = 0 if ms == 0 else max(ms, 200)
            ok(c)
            return out
        if need_pin and not self.authed:
            err(c, "auth required")
            return out
        r = h.rotor
        if c == "pos":
            az = d.get("az")
            if not isinstance(az, (int, float)) or isinstance(az, bool):
                err(c, "az")
                return out
            el = d.get("el")
            if isinstance(el, (int, float)) and not isinstance(el, bool) and r.has_el:
                good = r.set_pos(float(az), min(max(float(el), r.min_el), r.max_el))
            else:
                good = r.set_az(float(az))
            ok(c) if good else err(c, "rejected")
        elif c == "stop":
            r.stop()
            ok(c)
        elif c == "park":
            ok(c) if r.park() else err(c, "rejected")
        elif c == "clear":
            h.clear_fault()
            ok(c)
        elif c == "move":
            dr = parse_move_dir(d.get("d", ""))
            if not dr:
                err(c, "d")
            elif r.move(dr, int(d.get("s", 0))):
                ok(c)
            else:
                err(c, "rejected")
        elif c in ("cal", "here"):
            err(c, "only direct drive")
        elif c == "cfgget":
            cfg = h.config_json()
            n = max(1, (len(cfg) + CFG_CHUNK - 1) // CFG_CHUNK)
            for i in range(n):
                out.append(dumps({"e": "cfg", "i": i, "n": n, "d": cfg[i * CFG_CHUNK:(i + 1) * CFG_CHUNK]}))
        elif c == "cfgset":
            i, n, data = d.get("i", -1), d.get("n", 0), d.get("d", "")
            if not isinstance(i, int) or not isinstance(n, int) or i < 0 or n <= 0 or n > 200 or i >= n:
                err(c, "i/n")
                return out
            if i == 0:
                self._cfg_in, self._cfg_next = "", 0
            if i != self._cfg_next:
                self._cfg_in, self._cfg_next = "", 0
                err(c, "order")
                return out
            self._cfg_in += str(data)
            self._cfg_next += 1
            if len(self._cfg_in) > 16384:
                self._cfg_in, self._cfg_next = "", 0
                err(c, "too big")
                return out
            if i + 1 < n:
                out.append(dumps({"e": "ok", "c": c, "i": i}))
                return out
            e = h.apply_config(self._cfg_in)
            self._cfg_in, self._cfg_next = "", 0
            if e:
                err(c, e)
            else:
                out.append(dumps({"e": "ok", "c": c, "i": i, "done": True}))
        elif c == "reboot":
            ok(c)
            h.reboot()
        else:
            err(c, "unknown")
        return out

    def tick(self, h: ApiHost) -> list[str]:
        if not self.sub_ms:
            return []
        now = time.monotonic()
        moving = bool(h.rotor.status() & (ST_MOVING | ST_BUSY))
        every = self.sub_ms if moving else max(self.sub_ms, 5000)
        if moving != self._was_moving or (now - self._last_evt) * 1000 >= every:
            self._was_moving = moving
            self._last_evt = now
            return [pos_event(h)]
        return []
