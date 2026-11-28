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
        self._hlat = self._hlon = None    # posicion congelada mientras esta parado (filtro hold)
        self.held = False                 # True = esta devolviendo la posicion congelada
        self._moving = False              # estado del filtro de velocidad con histeresis (como el tracker)
        self._above = self._below = 0     # lecturas seguidas por encima / por debajo del umbral

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
                sp = d.get("speed")
                flat, flon = self._hold(float(la), float(lo), float(sp) if _ok(sp) else None)
                self.mode, self.lat, self.lon, self.fix_t = mode, flat, flon, now
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

    def _hold(self, la: float, lo: float, spd: float | None) -> tuple[float, float]:
        """Congela la posicion con el equipo parado para que el baile del fix no haga bailar el apuntado.

        Mismo filtro de velocidad que el tracker APRS LoRa+ (gps_extras.cpp): histeresis con dos umbrales y 3 lecturas
        seguidas (pasa a "en marcha" por encima de holdStart, vuelve a "parado" por debajo de holdStop). Parado devuelve
        la posicion congelada; en marcha sigue el fix en vivo (coche / barco). Un salto grande sin velocidad fiable
        (> holdM) tambien reubica la referencia.
        """
        if not self.cfg.get("hold", True):
            self._hlat, self._hlon, self.held, self._moving = la, lo, False, False
            return la, lo
        if self._hlat is None:
            self._hlat, self._hlon = la, lo
        if spd is not None:                                      # histeresis con 3 lecturas, como el tracker
            if not self._moving:
                self._above = self._above + 1 if spd >= float(self.cfg.get("holdStart", 0.97)) else 0
                if self._above >= 3:
                    self._moving, self._above, self._below = True, 0, 0
            else:
                self._below = self._below + 1 if spd < float(self.cfg.get("holdStop", 0.5)) else 0
                if self._below >= 3:
                    self._moving, self._above, self._below = False, 0, 0
        far = _dist_m(self._hlat, self._hlon, la, lo) > float(self.cfg.get("holdM", 15.0))
        if self._moving or far:                                 # en marcha o salto real: referencia en vivo
            self._hlat, self._hlon, self.held = la, lo, False
            if far and not self._moving:
                self._above = self._below = 0
        else:
            self.held = True                                    # parado: posicion congelada
        return self._hlat, self._hlon

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
             "pps": now - self.pps_t < PPS_AGE, "held": bool(p) and self.held}
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
                await self.configure_ublox()
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

    # ---- static hold del receptor u-blox (Uputronics GPS HAT) -----------------------------------------------------------
    async def configure_ublox(self):
        """Manda al receptor el static hold, como el tracker (gps_utils.cpp): M10 por UBX-CFG-VALSET, M8 por UBX-CFG-NAV5.

        Via socket de control de gpsd: `&<dev>=<hex>` escribe los bytes crudos en el receptor. Best-effort: si gpsd no
        tiene socket de control, no es un u-blox o rechaza el mensaje, se queda el filtro hold por software.
        """
        g = self.cfg
        chip = str(g.get("ublox", "")).lower()
        cms, dist = int(g.get("staticHoldCms", 0)), int(g.get("staticHoldDistM", 0))
        if chip not in ("m8", "m10", "x20") or not g.get("ctrlSock") or cms <= 0:
            return
        if chip in ("m10", "x20"):                            # base de datos de configuracion (M10, X20): claves CFG-MOT
            frame = _ubx_valset([(0x20250038, cms),           # CFG-MOT-GNSSSPEED_THRS (cm/s)
                                 (0x3025003B, dist)])         # CFG-MOT-GNSSDIST_THRS (m)
        else:
            frame = _ubx_cfg_nav5_hold(cms, dist)            # u-blox 8: CFG-NAV5 (solo static hold)
        cmd = ("&%s=%s\r\n" % (g.get("dev", "/dev/serial0"), frame.hex())).encode("ascii")
        try:
            r, w = await asyncio.wait_for(asyncio.open_unix_connection(g["ctrlSock"]), 3)
            w.write(cmd)
            await w.drain()
            resp = await asyncio.wait_for(r.readline(), 3)
            w.close()
            log.info("u-blox %s static hold: %d cm/s, %d m (%s)", chip.upper(), cms, dist,
                     resp.decode("ascii", "replace").strip() or "sin respuesta")
        except (OSError, asyncio.TimeoutError) as e:
            log.debug("static hold no aplicado: %s", e)


def _ubx(cls: int, mid: int, payload: bytes) -> bytes:
    """Enmarca un mensaje UBX: B5 62 + clase + id + longitud (LE) + payload + suma de Fletcher de 8 bits."""
    body = bytes([cls, mid]) + len(payload).to_bytes(2, "little") + bytes(payload)
    ck_a = ck_b = 0
    for b in body:                                           # suma sobre clase..payload
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return b"\xB5\x62" + body + bytes([ck_a, ck_b])


def _ubx_valset(items: list[tuple[int, int]]) -> bytes:
    """UBX-CFG-VALSET (0x06 0x8A) en capas RAM + BBR, como ubxValset del tracker (u-blox M10).

    El tamano del valor sale de los bits 30:28 de la clave: 1/2 = 1 byte, 3 = 2 bytes, 4 = 4 bytes.
    """
    payload = bytearray([0x00, 0x03, 0x00, 0x00])            # version 0, capas RAM (bit0) + BBR (bit1)
    for key, val in items:
        size = {3: 2, 4: 4}.get((key >> 28) & 0x07, 1)
        payload += key.to_bytes(4, "little")
        payload += (val & ((1 << (8 * size)) - 1)).to_bytes(size, "little")
    return _ubx(0x06, 0x8A, bytes(payload))


def _ubx_cfg_nav5_hold(cms: int, dist_m: int) -> bytes:
    """UBX-CFG-NAV5 (0x06 0x24) con solo el static hold, para u-blox 8 (interfaz antigua por mensajes, sin base de datos).

    mask = 0x0040 (bit 6, staticHoldMask): aplica unicamente staticHoldThresh (cm/s, offset 22, U1) y staticHoldMaxDist
    (m, offset 28, U2); el resto del payload va a 0 y no toca el modelo dinamico ni la elevacion minima.
    """
    p = bytearray(36)
    p[0:2] = (0x0040).to_bytes(2, "little")                 # solo static hold
    p[22] = min(max(cms, 0), 255)                           # staticHoldThresh (cm/s)
    p[28:30] = min(max(dist_m, 0), 65535).to_bytes(2, "little")   # staticHoldMaxDist (m)
    return _ubx(0x06, 0x24, bytes(p))


def _ok(v) -> bool:
    return isinstance(v, (int, float)) and math.isfinite(v)


def _dist_m(la1: float, lo1: float, la2: float, lo2: float) -> float:
    """Distancia en metros (equirectangular, de sobra a estas escalas)."""
    k = math.cos(math.radians((la1 + la2) * 0.5))
    return math.hypot((lo2 - lo1) * k, la2 - la1) * 111320.0
