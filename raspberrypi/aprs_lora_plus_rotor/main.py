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
"""Demonio APRS LoRa+ Rotor para Raspberry Pi (Zero 2 W, 3B / 3B+, 4, 5):

    rotctld de Hamlib (127.0.0.1:4535, cualquier rotor de Hamlib)
        ^
    demonio ── rotctld publico (4533) · JSON por TCP (4534) · WebUI (8080) · BLE · SPP · puertos serie · APRS por KISS
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import signal
import socket
import sys
import time

from . import __version__, antennas, board, geo, sat
from .api import ApiHost, ApiSession, pos_event  # noqa: F401
from .aprs import AprsConfig, AprsControl, LORA_HEADER, parse_position, status_line
from .bluez import Bluetooth
from .cat import Cat
from .config import DEFAULT_PATH, Config
from .hamlib import HamlibRotor, RotctldProcess
from .kiss import KissDecoder, ax25_to_tnc2, kiss_encode, tnc2_to_ax25
from .protocols import RotctlSession, make_server
from .serialport import SerialPort
from .web import WebServer
from .heading import HeadingRotor
from .lan import Lan
from .wind import WindGuard, WindRotor
from .sky import SkyRotor, P2P, SAT
from .stepper import StepperRotor
from .gnss import OwnGps

log = logging.getLogger("rotor")


class Daemon(ApiHost):
    def __init__(self, cfg: Config):
        self.cfg = cfg
        d = cfg.data
        self.wind = WindGuard(d["wind"])
        park = (float(d["park"]["az"]), float(d["park"]["el"]))
        if d["driver"] == "stepper":                       # motores paso a paso en la propia Pi (Waveshare Stepper Motor HAT)
            self.proc = None
            self.base = StepperRotor(d["stepper"], park)
        else:
            self.proc = RotctldProcess(d["hamlib"])
            self.base = HamlibRotor(d["hamlib"], park)
        self.gps = OwnGps(d["gps"])                        # GPS propio (Uputronics GPS/RTC HAT) por gpsd
        # tracker / app / Hamlib -> sol / luna -> rumbo (barco o coche) -> viento -> rotctld de Hamlib o motores
        self.heading = HeadingRotor(WindRotor(self.base, self.wind), bool(d["lan"]["heading"]), float(d["lan"]["headStep"]))
        sk = d["sky"]
        self.sky = SkyRotor(self.heading, float(sk["step"]), float(sk["minEl"]), bool(sk["refraction"]),
                            curvature=bool(d["p2p"]["curv"]))
        self.sky.peer_src = d["p2p"]["src"]
        self.satdb = sat.SatDb(d["sat"])                       # TLE amateur: lista y pasos de satelites
        self.sky.horizon_h = float(d["sat"]["horizonH"])
        if d["sat"]["name"]:
            s0 = self.satdb.get(d["sat"]["name"])
            if s0:
                self.sky.set_sat(s0)
        self._tle_task = None
        self.rotor = self.sky
        self.lan = Lan(d["lan"], d["aprs"], self.wind, self.heading)
        self.lan.on_park = lambda: (self.rotor.stop(), self.rotor.park())
        self.lan.own_pos = self.gps.position
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
        return {"dev": "APRS LoRa+ Rotor", "ver": __version__, "board": "raspberrypi", "chip": board.model(),
                "family": board.family(board.model()),
                "name": d["name"], "drv": "stepper" if self.proc is None else f"hamlib {d['hamlib']['model']}",
                "call": d["aprs"]["call"],
                "caps": {"wifi": True, "ble": bool(d["bt"]["ble"]), "spp": bool(d["bt"]["spp"]),
                         "lora": bool(d["aprs"]["enabled"]), "cal": self.proc is None, "move": True, "cat": bool(self.cat),
                         "wind": True, "lan": True, "sky": True, "p2p": True, "sat": True, "gps": bool(d["gps"]["on"])},
                "ip": _ip(), "rotctld": d["net"]["rotctldPort"] if d["net"]["rotctld"] else 0,
                "up": int(time.monotonic() - self.started), "utc": int(time.time()),
                **({"hamlib": self.proc.last_error or "ok"} if self.proc else {}),
                **({"gps": self.gps.info()} if self.gps.info() else {}),
                "rigctld": d["cat"]["serverPort"] if self.cat and d["cat"]["server"] else 0,
                **({"rig": self.cat.proc.last_error or "ok"} if self.cat and self.cat.proc else {}),
                **({"lan": self.lan.info()} if self.lan.info() else {})}

    def pos_extra(self) -> dict:
        r = self.rotor
        self.wind.tick()
        e = {"f": "OK" if r.link_ok else "NO LINK"}
        if isinstance(self.base, StepperRotor):            # motores: fallo del final de carrera o posicion sin fijar
            e["f"] = self.base.fault or ("OK" if self.base.known else "NOPOS")
        if e["f"] == "OK" and self.wind.locked:
            e["f"] = "WIND"
        if r.taz is not None:
            e["taz"] = round(r.taz, 1)
        if r.tel is not None and r.has_el:
            e["tel"] = round(r.tel, 1)
        if self.aprs.last_result:
            e["aprs"] = self.aprs.last_result
        h = self.heading.info_event(bool(self.cfg.data["lan"]["heading"]))
        if h:
            h["dec"] = round(self.lan.declination(), 1)        # declinacion que se suma al rumbo magnetico
            e["hdg"] = h
        return e

    def sky_start(self, target: int) -> str:
        if target == P2P:
            e = self._p2p_peer()
            if e:
                return e
        if target == SAT and self.sky.sat is None:            # sin elegir en esta sesion: usa el de la config si lo hay
            s0 = self.satdb.get(self.cfg.data["sat"]["name"])
            if s0 is None:
                return "no satellite selected"
            self.sky.set_sat(s0)
        alt = self.gps.alt if (self.gps.position() and self.gps.alt is not None) else 0.0
        return self.sky.start(target, self.lan.position(), alt=alt or 0.0)

    def _sat_pos(self) -> tuple[float, float, float] | None:
        p = self.lan.position()
        if not p:
            return None
        alt = self.gps.alt if (self.gps.position() and self.gps.alt is not None) else 0.0
        return p[0], p[1], alt or 0.0

    def list_sats(self, group=None, active: bool = False) -> list:
        pos = self._sat_pos()
        return self.satdb.passes(pos[0], pos[1], pos[2], group=group, active=active) if pos else []

    def sat_info(self) -> dict:
        return {"updated": int(self.satdb.updated), "n": len(self.satdb.sats),
                "name": self.sky.sat.name if self.sky.sat else "", "haspos": self._sat_pos() is not None,
                "groups": [list(g) for g in sat.GROUPS], "amsat": self.satdb.amsat_on,
                "amsatAt": int(self.satdb.active_at), "nactive": len(self.satdb.active_keys)}

    def select_sat(self, key: str) -> str:
        s = self.satdb.get(key)
        if s is None:
            return "unknown satellite"
        self.sky.set_sat(s)
        self.cfg.data["sat"]["name"] = s.name
        return ""

    def update_tle(self) -> str:
        if self._tle_task and not self._tle_task.done():
            return ""                                         # ya descargando
        self._tle_task = asyncio.create_task(self._tle_worker())
        return ""

    async def _tle_worker(self):
        loop = asyncio.get_event_loop()
        err = await loop.run_in_executor(None, self.satdb.update)
        if err:
            logging.warning("[SAT] TLE: %s", err)
        elif self.cfg.data["sat"]["name"] and self.sky.sat:   # re-enlaza el satelite elegido al TLE nuevo
            s = self.satdb.get(self.cfg.data["sat"]["name"])
            if s:
                self.sky.set_sat(s)
        aerr = await loop.run_in_executor(None, self.satdb.update_amsat)   # estado AMSAT (satelites activos)
        if aerr:
            logging.warning("[SAT] AMSAT: %s", aerr)

    def _p2p_peer(self) -> str:
        """Carga la posicion del otro extremo segun p2p.src (fija de la config; net/aprs = ya recibida)."""
        pp = self.cfg.data["p2p"]
        self.sky.peer_src = pp["src"]
        if pp["src"] == "fixed":
            if pp["loc"]:
                ll = geo.locator_to_latlon(pp["loc"])
                if ll is None:
                    return "p2p.loc invalid"
                self.sky.set_peer(ll[0], ll[1], float(pp["alt"]), "fixed")
                return ""
            if pp["lat"] or pp["lon"]:
                self.sky.set_peer(float(pp["lat"]), float(pp["lon"]), float(pp["alt"]), "fixed")
                return ""
            # sin coordenadas fijas: usa la ultima posicion recibida (peer / app) si la hay
        if self.sky.peer is None:
            return "no peer position (p2p.loc / coords, app or APRS beacon of p2p.call)"
        return ""

    def set_peer(self, lat: float, lon: float, alt: float = 0.0) -> str:
        self.sky.set_peer(lat, lon, alt, "net")              # posicion en vivo desde la app / red
        return ""

    async def sky_loop(self):
        """Seguir el sol, la luna, P2P o un satelite: la posicion de la estacion puede venir de la red (barco, coche).
        Con un satelite se refresca a 10 Hz (se mueve rapido); el resto, cada segundo."""
        while True:
            fast = False
            if self.sky.tracking:
                p = self.lan.position()
                if p:
                    self.sky.lat, self.sky.lon = p
                self.sky.tick()
                fast = self.sky.target == SAT
            await asyncio.sleep(0.1 if fast else 1.0)

    async def tle_loop(self):
        """Descarga el TLE al arrancar si falta o esta viejo, y lo refresca cada sat.refreshH horas."""
        every = float(self.cfg.data["sat"]["refreshH"]) * 3600.0
        if every <= 0:
            return
        while True:
            if not self.satdb.sats or time.time() - self.satdb.updated > every:
                self.update_tle()
            await asyncio.sleep(max(1800.0, every / 4.0))

    def config_json(self) -> str:
        return self.cfg.to_json(True)

    def apply_config(self, text: str) -> str:
        e = self.cfg.apply_json(text)
        if not e:
            asyncio.get_running_loop().call_later(1.5, self.reboot)
        return e

    def raw(self) -> dict:
        r = {"link": self.rotor.link_ok}
        if isinstance(self.base, StepperRotor):
            r.update(self.base.raw())
        return r

    def here(self, axis: str, deg: float) -> str:
        return self.base.here(axis, deg) if isinstance(self.base, StepperRotor) else "only direct drive"

    def home(self) -> str:
        return self.base.home() if isinstance(self.base, StepperRotor) else "no home switch"

    def list_antennas(self) -> list:
        return antennas.list_antennas()

    def select_antenna(self, name: str) -> str:
        """Carga el perfil de una antena sobre la config del HAT y reinicia (los pines del HAT se conservan)."""
        prof = antennas.get(name)
        if prof is None:
            return "unknown antenna"
        patch = {"driver": "stepper", "stepper": {**prof["stepper"], "antenna": name}}
        return self.apply_config(json.dumps(patch))

    def clear_fault(self):
        self.wind.release()
        if isinstance(self.base, StepperRotor):
            self.base.clear()

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
        pp = self.cfg.data["p2p"]                             # P2P por APRS: baliza de posicion del otro extremo
        if pp["src"] == "aprs" and pp["call"]:
            peer = parse_position(tnc2, pp["call"])
            if peer:
                self.sky.set_peer(peer[0], peer[1], peer[2], "aprs")
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
            lat, lon = self.lan.position() or (0.0, 0.0)          # la de la red local (lan.pos) o la configurada
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
        if self.proc:
            self.proc.start()
        asyncio.create_task(self.rotor.run())
        asyncio.create_task(self.gps.run())
        asyncio.create_task(self.lan.run())
        asyncio.create_task(self.sky_loop())
        asyncio.create_task(self.tle_loop())
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
        if self.proc:
            await self.proc.stop()
        if isinstance(self.base, StepperRotor):
            self.base.close()                            # quita la corriente y guarda la posicion
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


def main(argv=None):
    ap = argparse.ArgumentParser(description="APRS LoRa+ Rotor (Raspberry Pi)")
    ap.add_argument("-c", "--config", default=os.environ.get("ROTOR_CONFIG", DEFAULT_PATH))
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--print-default", action="store_true", help="muestra la configuracion por defecto y sale")
    a = ap.parse_args(argv)
    cfg = Config(a.config)
    if a.print_default:
        print(cfg.to_json(True))                                           # SOLO el JSON a stdout (lo usa install.sh)
        return 0
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    print("[INFO][Main] APRS LoRa+ (c) 2026 Manuel - EA7JRS", flush=True)   # la misma linea en todos los equipos APRS LoRa+
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
