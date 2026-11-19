# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""GPS propio de la caja (Uputronics GPS/RTC HAT u otro receptor que lea gpsd): posicion de la estacion y estado del PPS.

gpsd lee el u-blox por /dev/serial0 (GPIO14 / 15) y el PPS por /dev/pps0 (GPIO18); NTPsec toma la hora de gpsd (SHM 0 y 1,
raspberrypi/ntpsec.conf) y el reloj RV-3028 del HAT guarda la hora sin red. Aqui solo se usa la posicion: manda sobre la de
la red local (lan.pos) y sobre la configurada (aprs.lat / aprs.lon) para la baliza, el sol y la luna, la declinacion y el
tiempo de internet. Cliente del JSON de gpsd con la biblioteca estandar.
"""

from __future__ import annotations

import asyncio
import json
import logging
import math
import time

log = logging.getLogger("gnss")

WATCH = b'?WATCH={"enable":true,"json":true,"pps":true};\r\n'
FIX_AGE = 10.0       # s: una posicion mas vieja ya no cuenta
PPS_AGE = 3.0


class OwnGps:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.mode = 0                     # 0/1 sin posicion, 2 = 2D, 3 = 3D
        self.lat = self.lon = None
        self.alt: float | None = None
        self.sats_used = 0
        self.sats_seen = 0
        self.fix_t = 0.0
        self.pps_t = 0.0
        self.connected = False
        self.device = ""

    # ---- datos de gpsd --------------------------------------------------------------------------------------------------
    def feed(self, line: str, now: float | None = None):
        now = time.monotonic() if now is None else now
        try:
            d = json.loads(line)
        except ValueError:
            return
        if not isinstance(d, dict):
            return
        cl = d.get("class")
        if cl == "TPV":
            mode = int(d.get("mode", 0) or 0)
            la, lo = d.get("lat"), d.get("lon")
            if mode >= 2 and _ok(la) and _ok(lo):
                self.mode, self.lat, self.lon, self.fix_t = mode, float(la), float(lo), now
                a = d.get("altMSL", d.get("alt"))
                self.alt = float(a) if mode == 3 and _ok(a) else None
            else:
                self.mode = mode
            if d.get("device"):
                self.device = str(d["device"])
        elif cl == "SKY":
            sats = d.get("satellites")
            if isinstance(sats, list):
                self.sats_seen = len(sats)
                self.sats_used = sum(1 for s in sats if isinstance(s, dict) and s.get("used"))
            elif d.get("uSat") is not None:
                self.sats_used, self.sats_seen = int(d.get("uSat") or 0), int(d.get("nSat") or 0)
        elif cl == "PPS":
            self.pps_t = now

    def position(self, now: float | None = None) -> tuple[float, float] | None:
        now = time.monotonic() if now is None else now
        if self.cfg["on"] and self.mode >= 2 and self.lat is not None and now - self.fix_t < FIX_AGE:
            return self.lat, self.lon
        return None

    def info(self, now: float | None = None) -> dict | None:
        if not self.cfg["on"]:
            return None
        now = time.monotonic() if now is None else now
        p = self.position(now)
        i = {"link": self.connected, "fix": self.mode if p else 0, "sats": self.sats_used, "seen": self.sats_seen,
             "pps": now - self.pps_t < PPS_AGE}
        if p:
            i["lat"], i["lon"] = round(p[0], 6), round(p[1], 6)
            if self.alt is not None:
                i["alt"] = round(self.alt, 1)
        return i

    # ---- conexion -------------------------------------------------------------------------------------------------------
    async def run(self):
        if not self.cfg["on"]:
            return
        while True:
            try:
                r, w = await asyncio.wait_for(asyncio.open_connection(self.cfg["host"], int(self.cfg["port"])), 5)
                w.write(WATCH)
                await w.drain()
                self.connected = True
                log.info("gpsd %s:%s", self.cfg["host"], self.cfg["port"])
                while True:
                    line = await asyncio.wait_for(r.readline(), 30)
                    if not line:
                        break
                    self.feed(line.decode("utf-8", "replace"))
                w.close()
            except (OSError, asyncio.TimeoutError) as e:
                log.debug("gpsd: %s", e)
            self.connected = False
            await asyncio.sleep(5)


def _ok(v) -> bool:
    return isinstance(v, (int, float)) and math.isfinite(v)
