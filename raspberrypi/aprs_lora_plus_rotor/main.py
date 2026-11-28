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
import subprocess
import sys
import threading
import time

from . import __version__, antennas, astro, board, geo, sat, pointing, peak, drift
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
from .attitude import AttitudeRotor
from .lan import Lan
from .wind import WindGuard, WindRotor
from .sky import SkyRotor, P2P, SAT, RADEC
from .stepper import StepperRotor
from . import stepper
from .gnss import OwnGps

log = logging.getLogger("rotor")

# Ayudante que escribe install.sh con la ruta del repo y las opciones usadas: lo lanza el boton "Actualizar" de la WebUI.
UPDATE_SCRIPT = "/opt/aprs-lora-plus-rotor/update.sh"


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
        # modelo de apuntado (corrige el desalineado mecanico), por dentro de todo, lo mas cerca del rotor
        self.pmodel = pointing.PointingModel(d["pointing"])
        self.pbase = pointing.PointingRotor(self.base, self.pmodel, bool(d["pointing"]["on"]))
        # tracker / app / Hamlib -> sol / luna -> rumbo (barco o coche) -> viento -> apuntado -> rotctld de Hamlib o motores
        at = d["att"]
        self.att = AttitudeRotor(WindRotor(self.pbase, self.wind), bool(at["on"]), float(at["step"]), float(at["staleS"]),
                                 int(at["rollSign"]), int(at["pitchSign"]), int(at["yawSign"]),
                                 float(at["rollZero"]), float(at["pitchZero"]), float(at["yawZero"]),
                                 bool(at["level"]), int(at["xSign"]))
        self.heading = HeadingRotor(self.att, bool(d["lan"]["heading"]), float(d["lan"]["headStep"]))
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
        self._peak: dict = {}                              # estado del auto-pico con el SDR
        self._drift: dict = {}                             # estado del barrido por deriva

    # ---- ApiHost --------------------------------------------------------------------------------------------------------
    def info(self) -> dict:
        d = self.cfg.data
        return {"dev": "APRS LoRa+ Rotor", "ver": __version__, "board": "raspberrypi", "chip": board.model(),
                "family": board.family(board.model()),
                "name": d["name"], "drv": "stepper" if self.proc is None else f"hamlib {d['hamlib']['model']}",
                "call": d["aprs"]["call"],
                "caps": {"wifi": True, "ble": bool(d["bt"]["ble"]), "spp": bool(d["bt"]["spp"]),
                         "lora": bool(d["aprs"]["enabled"]), "cal": self.proc is None, "move": True, "cat": bool(self.cat),
                         "wind": True, "lan": True, "sky": True, "p2p": True, "sat": True, "att": True, "gps": bool(d["gps"]["on"]),
                         "update": os.path.exists(UPDATE_SCRIPT)},
                "ip": _ip(), "rotctld": d["net"]["rotctldPort"] if d["net"]["rotctld"] else 0,
                "up": int(time.monotonic() - self.started), "utc": int(time.time()),
                **({"hamlib": self.proc.last_error or "ok"} if self.proc else {}),
                **({"gps": self.gps.info()} if self.gps.info() else {}),
                **({"hat": stepper.hat_info(d["stepper"])} if self.proc is None else {}),
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
            e["taz"] = round(r.taz, 2)
        if r.tel is not None and r.has_el:
            e["tel"] = round(r.tel, 2)
        if self.aprs.last_result:
            e["aprs"] = self.aprs.last_result
        h = self.heading.info_event(bool(self.cfg.data["lan"]["heading"]))
        if h:
            h["dec"] = round(self.lan.declination(), 1)        # declinacion que se suma al rumbo magnetico
            e["hdg"] = h
        a = self.att.info_event()
        if a:
            e["att"] = a
        if self._peak:
            e["peak"] = {k: v for k, v in self._peak.items() if k != "stop"}
        if self._drift:
            e["drift"] = {k: v for k, v in self._drift.items() if k != "stop"}
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

    def set_radec(self, d: dict) -> str:
        """Punto fijo del cielo para RADEC (radioastronomia, linea H1): RA/Dec (J2000) o coordenadas galacticas l/b."""
        def f(v):
            return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None
        ra, dec, l, b = f(d.get("ra")), f(d.get("dec")), f(d.get("l")), f(d.get("b"))
        name = str(d.get("name", ""))
        if l is not None and b is not None:
            ra, dec = astro.galactic_to_radec(l, b)
            if not name:
                name = f"l{l:g} b{b:g}"
        if ra is None or dec is None:
            return "" if self.sky.target == RADEC else "radec: ra/dec or l/b"
        self.sky.set_radec(ra, dec, name)
        return ""

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
                "groups": self.satdb.groups(), "amsat": self.satdb.amsat_on,
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

    def set_attitude(self, roll: float, pitch: float, yaw: float = 0.0) -> str:
        self.att.set_attitude(roll, pitch, yaw)              # escora, cabeceo y giro del mastil (IMU / giroscopo)
        return ""

    def att_zero(self) -> str:
        z = self.att.tare()                                 # la actitud cruda actual pasa a ser el 0 (nivel de montaje)
        self.cfg.data["att"].update(rollZero=z[0], pitchZero=z[1], yawZero=z[2])   # para poder guardarla en la config
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

    def sg_read(self) -> dict:
        return self.base.sg() if isinstance(self.base, StepperRotor) else {}

    def cal_mark(self, axis: str) -> str:
        return self.base.cal_mark(axis) if isinstance(self.base, StepperRotor) else "only direct drive"

    def cal_gain(self, axis: str, deg: float):
        return self.base.cal_gain(axis, deg) if isinstance(self.base, StepperRotor) else None

    def apply_gain(self, axis: str, gear: float) -> str:
        """Guarda la reduccion 'gear' calibrada del eje y reinicia (se reconstruye el rotor con el valor nuevo)."""
        if not isinstance(self.base, StepperRotor):
            return "only direct drive"
        if axis not in ("az", "el", "x"):
            return "axis"
        return self.apply_config(json.dumps({"driver": "stepper", "stepper": {axis: {"gear": float(gear)}}}))

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

    def point_inmarsat(self, lon, fix: bool) -> tuple[str, dict]:
        """Calibracion con un satelite geoestacionario Inmarsat. Sin 'fix' apunta a su az/el (con la posicion GPS);
        con 'fix' corrige el cero: la orientacion actual (afinada a mano al maximo de la baliza) pasa a valer ese az/el."""
        pos = self.lan.position()
        if not pos:
            return "no position", {}
        slon = float(lon) if lon is not None else float(self.cfg.data["inmarsat"]["lon"])
        if not -180.0 <= slon <= 180.0:
            return "lon", {}
        alt = self.gps.alt if (self.gps.position() and self.gps.alt is not None) else 0.0
        az, el, dist = geo.geostationary_azel(pos[0], pos[1], slon, alt or 0.0)
        info = {"az": round(az, 2), "el": round(el, 2), "km": round(dist), "lon": slon}
        if el < 0:
            return "below horizon", info
        if fix:
            if not isinstance(self.base, StepperRotor):
                return "only direct drive", info
            self.base.here("az", az)
            if self.base.has_el:
                self.base.here("el", el)
            info["fixed"] = True
        else:
            self.rotor.set_pos(az, el)
        return "", info

    # ---- modelo de apuntado ---------------------------------------------------------------------------------------------
    def _ref_skypos(self, d: dict):
        """Posicion verdadera (az, el) de la referencia: az/el dados, o un objetivo (sol / luna / Inmarsat)."""
        def f(v):
            return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None
        tgt = str(d.get("target", ""))
        if tgt in ("sun", "moon", "inmarsat"):
            pos = self.lan.position()
            if not pos:
                return None
            if tgt == "sun":
                return astro.sun_position(time.time(), pos[0], pos[1])
            if tgt == "moon":
                az, el, _ = astro.moon_position(time.time(), pos[0], pos[1])
                return az, el
            az, el, _ = geo.geostationary_azel(pos[0], pos[1], float(self.cfg.data["inmarsat"]["lon"]))
            return az, el
        az, el = f(d.get("az")), f(d.get("el"))
        return (az, el) if az is not None and el is not None else None

    def point_add(self, d: dict) -> tuple[str, dict]:
        """Anade una referencia al modelo de apuntado: la posicion verdadera y el angulo actual del rotor (ya centrado)."""
        sky_pos = self._ref_skypos(d)
        if sky_pos is None:
            return ("no position" if d.get("target") else "az/el or target"), {}
        ra, re = self.base.get_pos()
        pt = [round(sky_pos[0] % 360.0, 3), round(sky_pos[1], 3), round(ra % 360.0, 3), round(re, 3)]
        self.cfg.data["pointing"]["points"].append(pt)
        self.cfg.save()
        return "", {"n": len(self.cfg.data["pointing"]["points"]), "sky": pt[:2], "rotor": pt[2:]}

    def point_fit(self) -> tuple[str, dict]:
        pts = [tuple(p) for p in self.cfg.data["pointing"]["points"]]
        if not pts:
            return "no points", {}
        m = pointing.PointingModel.fit(pts)
        self.pmodel.p.update(m.p)
        self.pbase.enabled = True
        self.cfg.data["pointing"].update({**m.p, "on": True})
        self.cfg.save()
        return "", {"params": {k: round(v, 4) for k, v in m.p.items()}, "rms": round(m.residual(pts), 3), "n": len(pts)}

    def point_clear(self) -> dict:
        self.cfg.data["pointing"]["points"] = []
        self.cfg.save()
        return {"n": 0}

    def point_enable(self, on: bool):
        self.pbase.enabled = bool(on)
        self.cfg.data["pointing"]["on"] = bool(on)
        self.cfg.save()

    def point_info(self) -> dict:
        pm = self.cfg.data["pointing"]
        pts = [tuple(p) for p in pm["points"]]
        return {"on": self.pbase.enabled, "n": len(pts),
                "params": {k: round(float(pm[k]), 4) for k in ("ia", "ca", "np", "ie", "tf")},
                "rms": round(self.pmodel.residual(pts), 3) if pts else 0.0}

    # ---- auto-pico con el SDR -------------------------------------------------------------------------------------------
    def peak_start(self) -> str:
        cfg = self.cfg.data["peak"]
        if not cfg["host"]:
            return "no probe (configura peak.host)"
        if self._peak.get("running"):
            return "already running"
        self._peak = {"running": True, "stop": False, "n": 0}
        threading.Thread(target=self._peak_scan, name="peak", daemon=True).start()
        return ""

    def peak_stop(self):
        if self._peak.get("running"):
            self._peak["stop"] = True

    def _peak_scan(self):
        cfg = self.cfg.data["peak"]
        probe = peak.Probe(cfg["host"], int(cfg["port"]), cfg["kind"], cfg["query"])
        self.rotor.stop()                                   # deja de seguir: el barrido manda
        c = self.base.get_pos()
        grid = peak.raster(float(cfg["span"]), float(cfg["step"]))
        dwell = max(0.2, float(cfg["dwell"]))
        samples = []
        for da, de in grid:
            if self._peak.get("stop"):
                break
            self.base.set_pos((c[0] + da) % 360.0, c[1] + de)
            time.sleep(dwell)
            p = probe.read()
            if p is not None:
                samples.append((da, de, p))
            self._peak["n"] = len(samples)
        if samples:
            da, de, pmax = peak.best(samples)
            self.base.set_pos((c[0] + da) % 360.0, c[1] + de)
            self._peak = {"running": False, "done": True, "daz": da, "del": de, "pmax": round(pmax, 2), "n": len(samples)}
        else:
            self._peak = {"running": False, "done": True, "n": 0, "err": "no readings"}

    # ---- barrido por deriva (radioastronomia H1) ------------------------------------------------------------------------
    def drift_start(self) -> str:
        if self._drift.get("running"):
            return "already running"
        pos = self.lan.position()
        if not pos:
            return "no position"
        dr = self.cfg.data["drift"]
        steps = drift.plan(pos[0], float(dr["decStart"]), float(dr["decEnd"]), float(dr["decStep"]))
        if not steps:
            return "nothing above horizon"
        self._drift = {"running": True, "stop": False, "i": 0, "n": len(steps)}
        threading.Thread(target=self._drift_scan, args=(steps,), name="drift", daemon=True).start()
        return ""

    def drift_stop(self):
        if self._drift.get("running"):
            self._drift["stop"] = True

    def _drift_scan(self, steps):
        dr = self.cfg.data["drift"]
        dwell = max(1.0, float(dr["dwell"]))
        path = dr["csv"]
        try:
            os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
            new = not os.path.exists(path)
            f = open(path, "a", encoding="utf-8")
            if new:
                f.write("utc,dec,az,el\n")
        except OSError as e:
            self._drift = {"running": False, "done": True, "err": str(e)}
            return
        for i, (dec, az, el) in enumerate(steps):
            if self._drift.get("stop"):
                break
            self._drift.update(i=i + 1, dec=dec)
            self.pbase.set_pos(az, el)                       # apuntado fijo (con el modelo), sin seguimiento
            f.write(f"{int(time.time())},{dec},{az},{el}\n")
            f.flush()
            end = time.monotonic() + dwell
            while time.monotonic() < end and not self._drift.get("stop"):
                time.sleep(0.5)
        f.close()
        self._drift = {"running": False, "done": True, "n": len(steps), "i": self._drift.get("i", 0)}

    def clear_fault(self):
        self.wind.release()
        if isinstance(self.base, StepperRotor):
            self.base.clear()

    def reboot(self):
        """Reinicia el demonio (systemd lo vuelve a arrancar con la configuracion nueva)."""
        self.stop_event.set()

    def self_update(self) -> str:
        """Lanza la actualizacion desde git (git pull + install.sh, que reinicia el servicio al final).
        Se lanza desacoplada para que sobreviva al reinicio del propio demonio. '' = lanzado; si no, el error."""
        if not os.path.exists(UPDATE_SCRIPT):
            return "no update script (reinstala con install.sh)"
        # systemd-run deja la actualizacion en una unidad transitoria, independiente de este servicio
        for argv in (["systemd-run", "--collect", "--unit=aprs-lora-plus-rotor-update", "/bin/sh", UPDATE_SCRIPT],
                     ["setsid", "/bin/sh", UPDATE_SCRIPT]):
            try:
                subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
                log.info("actualizacion lanzada: %s", argv[0])
                return ""
            except FileNotFoundError:
                continue
            except Exception as e:  # noqa: BLE001
                return str(e)
        return "no se pudo lanzar la actualizacion"

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
