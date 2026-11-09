# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Configuracion del demonio: /etc/aprs-lora-plus-rotor/config.json (se edita entera desde la WebUI o la app)."""

from __future__ import annotations

import copy
import json
import os

from .aprs import b32key

from . import __version__ as VERSION
MASK = "********"
DEFAULT_PATH = "/etc/aprs-lora-plus-rotor/config.json"

DEFAULTS: dict = {
    "name": "APRS-Rotor",
    "pin": "123456",
    # rotctld de Hamlib: modelo (rotctl -l), puerto del controlador y velocidad. 603 = Yaesu GS-232B, 202 = EasyComm II,
    # 901 = SPID Rot2Prog, 1 = Dummy (pruebas sin rotor). minAz... = null: los que diga Hamlib
    "hamlib": {"model": 1, "device": "/dev/ttyUSB0", "speed": 9600, "conf": "", "port": 4535, "rotctld": "",
               "pollMs": 500, "minAz": None, "maxAz": None, "minEl": None, "maxEl": None, "hasEl": None},
    "park": {"az": 0.0, "el": 0.0},
    "net": {"rotctld": True, "rotctldPort": 4533, "apiPort": 4534, "web": True, "webPort": 8080,
            "webUser": "admin", "webPass": ""},
    # Bluetooth: BLE con el protocolo JSON (app) y/o SPP (Bluetooth clasico) con un protocolo de Hamlib para el tracker
    "bt": {"ble": True, "spp": False, "sppProto": 1, "adapter": "hci0"},
    # Puertos serie donde la Raspberry hace de controlador (p. ej. /dev/ttyGS0 de la Zero 2 W como USB gadget)
    "serialPorts": [],
    # APRS por un TNC KISS: "tcp:IP:8001" (tracker / iGate APRS LoRa+, Direwolf) o "serial:/dev/ttyACM0:115200"
    "aprs": {"enabled": False, "kiss": "tcp:127.0.0.1:8001", "call": "N0CALL-12", "tocall": "APZLRR", "path": "",
             "allow": "", "totp": "", "reply": True, "beaconMin": 0, "lat": 0.0, "lon": 0.0, "comment": "APRS LoRa+ Rotor"},
    # CAT de la emisora o del receptor: "none", "hamlib" (rigctld de Hamlib en 127.0.0.1:internalPort; model = rigctl -l,
    # p. ej. 3081 IC-9700, 1035 FT-991, 2014 TS-2000), "rigctl" (rigctld de la red: SDR++, gqrx) o "sdrconnect" (SDRplay)
    "cat": {"type": "none", "name": "", "model": 1, "device": "/dev/ttyUSB1", "speed": 19200, "civ": "", "conf": "",
            "internalPort": 4536, "rigctld": "", "host": "127.0.0.1", "port": 0, "pollMs": 300, "ptt": False,
            "pttMaxS": 180, "trackStep": 50, "server": True, "serverPort": 4532},
}

DEFAULTS["wind"] = {"on": False, "park": 15.0, "resume": 10.0, "hold": 15}   # proteccion por viento (wind.py)

SECRETS = [("pin",), ("net", "webPass"), ("aprs", "totp")]


def _merge(base: dict, new: dict) -> dict:
    out = copy.deepcopy(base)
    for k, v in new.items():
        if k in out and isinstance(out[k], dict) and isinstance(v, dict):
            out[k] = _merge(out[k], v)
        else:
            out[k] = v
    return out


def validate(c: dict) -> str:
    """Texto del error o '' si vale."""
    try:
        if not 1 <= len(c["name"]) <= 24:
            return "name 1-24"
        if c["pin"] and not 4 <= len(c["pin"]) <= 16:
            return "pin 4-16"
        h = c["hamlib"]
        int(h["model"]); int(h["speed"]); int(h["port"])
        for p in ("rotctldPort", "apiPort", "webPort"):
            if not 1 <= int(c["net"][p]) <= 65535:
                return "net." + p
        if int(c["bt"]["sppProto"]) not in range(5):
            return "bt.sppProto 0-4"
        for sp in c["serialPorts"]:
            if not sp.get("device") or int(sp.get("proto", 1)) not in range(5):
                return "serialPorts"
        k = c["cat"]
        if k["type"] not in ("none", "hamlib", "rigctl", "sdrconnect"):
            return "cat.type none | hamlib | rigctl | sdrconnect"
        int(k["model"]); int(k["speed"])
        for p in ("internalPort", "serverPort"):
            if not 1 <= int(k[p]) <= 65535:
                return "cat." + p
        if not 0 <= int(k["port"]) <= 65535:
            return "cat.port"
        if k["type"] in ("rigctl", "sdrconnect") and not k["host"]:
            return "cat.host"
        if not 1 <= int(k["pttMaxS"]) <= 3600:
            return "cat.pttMaxS 1-3600"
        ports = [int(c["net"]["rotctldPort"]), int(c["net"]["apiPort"]), int(c["net"]["webPort"]), int(c["hamlib"]["port"])]
        if k["type"] != "none" and k["server"] and int(k["serverPort"]) in ports:
            return "cat.serverPort"
        if k["type"] == "hamlib" and int(k["internalPort"]) in ports + [int(k["serverPort"])]:
            return "cat.internalPort"
        w = c["wind"]
        if not (3 <= float(w["park"]) <= 60 and 0 < float(w["resume"]) <= float(w["park"]) and 0 <= int(w["hold"]) <= 720):
            return "wind: park 3-60 m/s, 0 < resume <= park, hold 0-720 min"
        a = c["aprs"]
        if not 1 <= len(a["call"]) <= 9:
            return "aprs.call"
        if not (a["kiss"].startswith("tcp:") or a["kiss"].startswith("serial:")):
            return "aprs.kiss tcp:host:port | serial:/dev/x:baud"
        if a["totp"]:
            if len(b32key(a["totp"])) < 10:
                return "aprs.totp"
    except Exception as e:  # noqa: BLE001
        return f"config: {e}"
    return ""


class Config:
    def __init__(self, path: str = DEFAULT_PATH):
        self.path = path
        self.data = copy.deepcopy(DEFAULTS)

    def load(self) -> bool:
        try:
            with open(self.path, encoding="utf-8") as f:
                d = _merge(DEFAULTS, json.load(f))
        except FileNotFoundError:
            return False
        err = validate(d)
        if err:
            raise ValueError(err)
        self.data = d
        return True

    def save(self):
        os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=1, ensure_ascii=False)
        os.chmod(tmp, 0o600)
        os.replace(tmp, self.path)

    def to_json(self, secrets: bool = True) -> str:
        d = copy.deepcopy(self.data)
        d["ver"] = VERSION
        if not secrets:
            for p in SECRETS:
                o = d
                for k in p[:-1]:
                    o = o[k]
                if o.get(p[-1]):
                    o[p[-1]] = MASK
        return json.dumps(d, separators=(",", ":"), ensure_ascii=False)

    def apply_json(self, text: str) -> str:
        """Configuracion nueva (entera o parcial). Devuelve el error o ''. Los secretos "********" no cambian."""
        try:
            new = json.loads(text)
            if not isinstance(new, dict):
                return "bad json"
        except ValueError:
            return "bad json"
        new.pop("ver", None)
        for p in SECRETS:
            o = new
            for k in p[:-1]:
                o = o.get(k, {}) if isinstance(o, dict) else {}
            if isinstance(o, dict) and o.get(p[-1]) == MASK:
                o.pop(p[-1])
        d = _merge(self.data, new)
        d["aprs"]["call"] = str(d["aprs"]["call"]).upper()
        err = validate(d)
        if err:
            return err
        self.data = d
        self.save()
        return ""
