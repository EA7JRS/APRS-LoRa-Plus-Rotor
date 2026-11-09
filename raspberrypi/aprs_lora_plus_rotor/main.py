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
"""Demonio APRS LoRa+ Rotor para Raspberry Pi (Zero 2 W, 3, 4, 5):

    rotctld de Hamlib (127.0.0.1:4535, cualquier rotor de Hamlib)
        ^
    demonio ── rotctld publico (4533) · JSON por TCP (4534) · WebUI (8080) · BLE · SPP · puertos serie · APRS por KISS
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import os
import signal
import socket
import sys
import time

from . import __version__
from .api import ApiHost, ApiSession, pos_event  # noqa: F401
from .aprs import AprsConfig, AprsControl, LORA_HEADER, status_line
from .bluez import Bluetooth
from .cat import Cat
from .config import DEFAULT_PATH, Config
from .hamlib import HamlibRotor, RotctldProcess
from .kiss import KissDecoder, ax25_to_tnc2, kiss_encode, tnc2_to_ax25
from .protocols import RotctlSession, make_server
from .serialport import SerialPort
from .web import WebServer
from .wind import WindGuard, WindRotor

log = logging.getLogger("rotor")


class Daemon(ApiHost):
    def __init__(self, cfg: Config):
        self.cfg = cfg
        d = cfg.data
        self.proc = RotctldProcess(d["hamlib"])
        self.wind = WindGuard(d["wind"])
        self.rotor = WindRotor(HamlibRotor(d["hamlib"], (float(d["park"]["az"]), float(d["park"]["el"]))), self.wind)
        a = d["aprs"]
        self.aprs = AprsControl(AprsConfig(a["call"].upper(), a["tocall"], a["path"], a["allow"], a["totp"], bool(a["reply"])))
        self.kiss_writer = None
        self.kiss_serial: SerialPort | None = None
        self.started = time.monotonic()
        self.stop_event = asyncio.Event()
        self.bt: Bluetooth | None = None
        self.cat = Cat(d["cat"]) if d["cat"]["type"] != "none" else None

    # ---- ApiHost --------------------------------------------------------------------------------------------------------
    def info(self) -> dict:
        d = self.cfg.data
        return {"dev": "APRS LoRa+ Rotor", "ver": __version__, "board": "raspberrypi", "chip": _pi_model(),
                "name": d["name"], "drv": f"hamlib {d['hamlib']['model']}", "call": d["aprs"]["call"],
                "caps": {"wifi": True, "ble": bool(d["bt"]["ble"]), "spp": bool(d["bt"]["spp"]),
                         "lora": bool(d["aprs"]["enabled"]), "cal": False, "move": True, "cat": bool(self.cat),
                         "wind": True},
                "ip": _ip(), "rotctld": d["net"]["rotctldPort"] if d["net"]["rotctld"] else 0,
                "up": int(time.monotonic() - self.started), "utc": int(time.time()),
                "hamlib": self.proc.last_error or "ok",
                "rigctld": d["cat"]["serverPort"] if self.cat and d["cat"]["server"] else 0,
                **({"rig": self.cat.proc.last_error or "ok"} if self.cat and self.cat.proc else {})}

    def pos_extra(self) -> dict:
        r = self.rotor
        self.wind.tick()
        e = {"f": "OK" if r.link_ok else "NO LINK"}
        if e["f"] == "OK" and self.wind.locked:
            e["f"] = "WIND"
        if r.taz is not None:
            e["taz"] = round(r.taz, 1)
        if r.tel is not None and r.has_el:
            e["tel"] = round(r.tel, 1)
        if self.aprs.last_result:
            e["aprs"] = self.aprs.last_result
        return e

    def config_json(self) -> str:
        return self.cfg.to_json(True)

    def apply_config(self, text: str) -> str:
        e = self.cfg.apply_json(text)
        if not e:
            asyncio.get_running_loop().call_later(1.5, self.reboot)
        return e

    def raw(self) -> dict:
        return {"link": self.rotor.link_ok}

    def clear_fault(self):
        self.wind.release()

    def reboot(self):
        """Reinicia el demonio (systemd lo vuelve a arrancar con la configuracion nueva)."""
        self.stop_event.set()

    def pin(self) -> str:
        return self.cfg.data["pin"]

    # ---- servidores -----------------------------------------------------------------------------------------------------
    async def rotctld_client(self, reader, writer):
        s = RotctlSession()
        try:
            while data := await reader.read(256):
                out, close = s.feed(data, self.rotor)
                if out:
                    writer.write(out)
                    await writer.drain()
                if close:
                    break
        except ConnectionError:
            pass
        finally:
            writer.close()

    async def api_client(self, reader, writer):
        s = ApiSession()
        buf = b""

        async def ticker():
            while True:
                await asyncio.sleep(0.2)
                for l in s.tick(self):
                    writer.write((l + "\n").encode())
        t = asyncio.create_task(ticker())
        try:
            while data := await reader.read(512):
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    if line.strip():
                        for l in s.handle(line.decode("utf-8", "replace"), self):
                            writer.write((l + "\n").encode())
                if len(buf) > 2048:
                    buf = b""
                await writer.drain()
        except ConnectionError:
            pass
        finally:
            t.cancel()
            writer.close()

    async def serial_server(self, sp: dict):
        while True:
            port = SerialPort(sp["device"], int(sp.get("baud", 9600)))
            try:
                port.open()
                log.info("puerto serie %s (protocolo %s)", sp["device"], sp.get("proto", 1))
                srv = make_server(int(sp.get("proto", 1)))
                while data := await port.read():
                    out = srv.feed(data, self.rotor)
                    if out:
                        port.write(out)
            except OSError as e:
                log.warning("%s: %s", sp["device"], e)
            finally:
                port.close()
            await asyncio.sleep(5)

    # ---- APRS por KISS --------------------------------------------------------------------------------------------------
    def aprs_send(self, tnc2: str):
        try:
            frame = kiss_encode(tnc2_to_ax25(tnc2))
        except ValueError as e:
            log.warning("APRS: %s", e)
            return
        if self.kiss_writer:
            self.kiss_writer.write(frame)
        elif self.kiss_serial:
            self.kiss_serial.write(frame)

    def aprs_rx(self, ax25: bytes):
        tnc2 = ax25_to_tnc2(ax25)
        if not tnc2:
            return
        if tnc2.encode("latin-1", "replace").startswith(LORA_HEADER):
            tnc2 = tnc2[3:]
        mine, tx = self.aprs.on_frame(tnc2, rot=self.rotor)
        if mine:
            log.info("APRS: %s", self.aprs.last_result)
            for i, f in enumerate(tx):
                asyncio.get_running_loop().call_later(1.5 + 2.5 * i, self.aprs_send, f)

    async def aprs_link(self):
        spec = self.cfg.data["aprs"]["kiss"]
        while True:
            dec = KissDecoder()
            try:
                if spec.startswith("tcp:"):
                    _, host, port = spec.split(":", 2)
                    reader, self.kiss_writer = await asyncio.open_connection(host, int(port))
                    log.info("APRS: TNC KISS %s:%s", host, port)
                    while data := await reader.read(512):
                        for f in dec.feed(data):
                            self.aprs_rx(f)
                else:
                    _, dev, baud = spec.split(":", 2)
                    self.kiss_serial = SerialPort(dev, int(baud))
                    self.kiss_serial.open()
                    log.info("APRS: TNC KISS %s", dev)
                    while data := await self.kiss_serial.read():
                        for f in dec.feed(data):
                            self.aprs_rx(f)
            except (OSError, ValueError) as e:
                log.warning("APRS KISS: %s", e)
            finally:
                if self.kiss_writer:
                    self.kiss_writer.close()
                self.kiss_writer = None
                if self.kiss_serial:
                    self.kiss_serial.close()
                self.kiss_serial = None
            await asyncio.sleep(10)

    async def aprs_beacon(self):
        a = self.cfg.data["aprs"]
        every = int(a["beaconMin"]) * 60
        if every <= 0:
            return
        await asyncio.sleep(30)
        while True:
            st = status_line(self.rotor, "OK" if self.rotor.link_ok else "NO LINK")
            base = a["call"].upper() + ">" + a["tocall"] + (("," + a["path"]) if a["path"] else "")
            lat, lon = float(a["lat"]), float(a["lon"])
            if lat or lon:
                la, lo = abs(lat), abs(lon)
                pos = (f"!{int(la):02d}{(la - int(la)) * 60:05.2f}{'N' if lat >= 0 else 'S'}/"
                       f"{int(lo):03d}{(lo - int(lo)) * 60:05.2f}{'E' if lon >= 0 else 'W'}r")
                self.aprs_send(f"{base}:{pos}{st} {a['comment']}")
            else:
                self.aprs_send(f"{base}:>{st} {a['comment']}")
            await asyncio.sleep(every)

    # ---- arranque -------------------------------------------------------------------------------------------------------
    async def run(self):
        d = self.cfg.data
        self.proc.start()
        asyncio.create_task(self.rotor.run())
        net = d["net"]
        servers = []
        if net["rotctld"]:
            servers.append(await asyncio.start_server(self.rotctld_client, "0.0.0.0", int(net["rotctldPort"])))
            log.info("rotctld publico en el puerto %s", net["rotctldPort"])
        servers.append(await asyncio.start_server(self.api_client, "0.0.0.0", int(net["apiPort"])))
        if self.cat:
            asyncio.create_task(self.cat.run())
            if d["cat"]["server"]:
                servers.append(await asyncio.start_server(self.cat.rigctld_client, "0.0.0.0", int(d["cat"]["serverPort"])))
                log.info("rigctld de la emisora en el puerto %s", d["cat"]["serverPort"])
        if net["web"]:
            web = WebServer(self, int(net["webPort"]), net["webUser"], lambda: net["webPass"] or d["pin"])
            await web.start()
        for sp in d["serialPorts"]:
            asyncio.create_task(self.serial_server(sp))
        if d["aprs"]["enabled"]:
            asyncio.create_task(self.aprs_link())
            asyncio.create_task(self.aprs_beacon())
        if d["bt"]["ble"] or d["bt"]["spp"]:
            self.bt = Bluetooth(d["bt"], d["name"], self.rotor, self, ApiSession,
                                lambda: make_server(int(d["bt"]["sppProto"])))
            try:
                await self.bt.start()
            except Exception as e:  # noqa: BLE001
                log.error("Bluetooth: %s", e)
        await self.stop_event.wait()
        log.info("parando")
        for s in servers:
            s.close()
        await self.proc.stop()
        if self.cat and self.cat.proc:
            await self.cat.proc.stop()


def _ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return ""


def _pi_model() -> str:
    try:
        with open("/proc/device-tree/model", encoding="utf-8", errors="replace") as f:
            return f.read().strip("\x00\n ")
    except OSError:
        return "Linux"


def main(argv=None):
    ap = argparse.ArgumentParser(description="APRS LoRa+ Rotor (Raspberry Pi)")
    ap.add_argument("-c", "--config", default=os.environ.get("ROTOR_CONFIG", DEFAULT_PATH))
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--print-default", action="store_true", help="muestra la configuracion por defecto y sale")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    cfg = Config(a.config)
    if a.print_default:
        print(cfg.to_json(True))
        return 0
    try:
        if not cfg.load():
            cfg.save()
            log.info("configuracion por defecto creada en %s", a.config)
    except ValueError as e:
        log.error("configuracion no valida (%s): %s", a.config, e)
        return 2

    async def runner():
        dmn = Daemon(cfg)
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, dmn.stop_event.set)
        await dmn.run()
    asyncio.run(runner())
    return 0


if __name__ == "__main__":
    sys.exit(main())
