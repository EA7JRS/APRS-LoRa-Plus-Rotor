# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Protocolo de control JSON por lineas (docs/CONTROL_PROTOCOL.md), el mismo que rc_api.h del firmware."""

from __future__ import annotations

import json
import time

from . import geo, sky
from .protocols import ST_BUSY, ST_MOVING, parse_move_dir

API_VERSION = 1
CFG_CHUNK = 180


def dumps(d: dict) -> str:
    return json.dumps(d, separators=(",", ":"), ensure_ascii=False)


def _num(v):
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None


class ApiHost:
    """Lo implementa el demonio (main.Daemon)."""

    rotor = None
    cat = None          # cat.Cat o None
    wind = None         # wind.WindGuard o None
    sky = None          # sky.SkyRotor o None

    def sky_start(self, target: int) -> str: return "no sky"
    def set_peer(self, lat: float, lon: float, alt: float = 0.0) -> str: return "no sky"   # P2P: otra estacion
    def set_radec(self, d: dict) -> str: return "no sky"                          # RADEC: punto del cielo (ra/dec o l/b)
    def set_attitude(self, roll: float, pitch: float, yaw: float = 0.0) -> str: return "no attitude"  # IMU: escora / cabeceo / giro
    def att_zero(self) -> str: return "no attitude"                              # tara: la actitud actual pasa a 0

    def info(self) -> dict: return {}
    def pos_extra(self) -> dict: return {}
    def config_json(self) -> str: return "{}"
    def apply_config(self, text: str) -> str: return "not supported"
    def raw(self) -> dict: return {}
    def here(self, axis: str, deg: float) -> str: return "only direct drive"     # Corregir desfase
    def home(self) -> str: return "no home switch"                               # buscar los finales de carrera
    def list_antennas(self) -> list: return []                                   # selector de antenas (motores paso a paso)
    def select_antenna(self, name: str) -> str: return "not supported"
    def cal_mark(self, axis: str) -> str: return "only direct drive"             # calibracion de ganancia: marcar punto A
    def cal_gain(self, axis: str, deg: float) -> dict | None: return None        # calcular 'gear' real con los grados A->B
    def apply_gain(self, axis: str, gear: float) -> str: return "not supported"  # guardar la 'gear' calculada y reiniciar
    def point_inmarsat(self, lon, fix: bool) -> tuple[str, dict]: return "not supported", {}   # apuntar / fijar cero con Inmarsat
    def list_sats(self, group=None, active: bool = False) -> list: return []     # satelites del TLE con su proximo paso
    def sat_info(self) -> dict: return {}
    def select_sat(self, key: str) -> str: return "no sky"
    def update_tle(self) -> str: return "no sky"
    def clear_fault(self): pass
    def reboot(self): pass
    def self_update(self) -> str: return "not supported"             # actualizar desde git y reinstalar (Raspberry Pi); "" = lanzado
    def pin(self) -> str: return ""


def pos_event(h: ApiHost) -> str:
    r = h.rotor
    az, el = r.get_pos()
    st = r.status()
    d = {"e": "pos", "az": round(az, 2)}
    if r.has_el:
        d["el"] = round(el, 2)
    if r.has_x:
        xd = r.get_axis("x")
        if xd is not None:
            d["x"] = round(xd, 1)
    d["st"] = st
    d["mv"] = bool(st & ST_MOVING)
    d.update(h.pos_extra())
    if h.wind is not None:
        w = h.wind.info()
        if w:
            d["wind"] = w
    if h.sky is not None:
        t = h.sky.info_event()
        if t:
            d["trk"] = t
    return dumps(d)


class ApiSession:
    def __init__(self):
        self.authed = False
        self.sub_ms = 0
        self._cfg_in = ""
        self._cfg_next = 0
        self._last_evt = 0.0
        self._was_moving = False
        self._last_cat = 0.0

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
                 "lim": {"minAz": r.min_az, "maxAz": r.max_az, "minEl": r.min_el, "maxEl": r.max_el,
                         "minX": r.min_x, "maxX": r.max_x},
                 "hasEl": r.has_el, "hasX": r.has_x, "pin": need_pin}
            i.update(h.info())
            i["cat"] = bool(h.cat and h.cat.rig)
            i["wind"] = h.wind is not None
            i["sky"] = h.sky is not None
            out.append(dumps(i))
            return out
        if c == "get":
            out.append(pos_event(h))
            return out
        if c == "raw":
            out.append(dumps({"e": "raw", **h.raw()}))
            return out
        if c == "antennas":
            out.append(dumps({"e": "antennas", "list": h.list_antennas()}))
            return out
        if c == "sats":                                     # lista de satelites (nombre, az/el, proximo paso); grp = categoria, act = solo activos
            grp = d.get("grp") or None
            out.append(dumps({"e": "sats", "list": h.list_sats(grp, bool(d.get("act"))), **h.sat_info()}))
            return out
        if c == "sub":
            ms = int(d.get("ms", 1000))
            self.sub_ms = 0 if ms == 0 else max(ms, 200)
            ok(c)
            return out
        if c == "cat":
            if h.cat and h.cat.rig:
                out.append(dumps(h.cat.event()))
            else:
                err(c, "no cat")
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
        elif c == "axis":                                   # eje auxiliar (x = inclinacion / cross-level)
            deg = _num(d.get("deg"))
            if deg is None:
                err(c, "deg")
            elif r.set_axis(str(d.get("axis", "x")), float(deg)):
                ok(c)
            else:
                err(c, "rejected")
        elif c == "antenna":                                # selector: cargar el perfil de una antena y reiniciar
            e = h.select_antenna(str(d.get("name", "")))
            ok(c) if not e else err(c, e)
        elif c == "here":
            deg = _num(d.get("deg"))
            e = "deg" if deg is None else h.here(str(d.get("axis", "az")), deg)
            ok(c) if not e else err(c, e)
        elif c == "home":
            e = h.home()
            ok(c) if not e else err(c, e)
        elif c == "cal":
            err(c, "only direct drive")
        elif c == "calstep":                            # calibracion de ganancia de los motores paso a paso (dos puntos)
            axis = str(d.get("axis", "az"))
            if d.get("mark"):
                e = h.cal_mark(axis)
                ok(c) if not e else err(c, e)
            else:
                deg = _num(d.get("deg"))
                if deg is None:
                    err(c, "deg")
                    return out
                res = h.cal_gain(axis, float(deg))
                if res is None:
                    err(c, "mark first")
                    return out
                e = h.apply_gain(axis, res["gear"]) if d.get("apply") else ""
                ok(c, gear=res["gear"], spd=res["spd"], steps=res["steps"]) if not e else err(c, e)
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
        elif c == "catset":
            self._catset(d, h, ok, err)
        elif c == "cattrack":
            if not (h.cat and h.cat.rig):
                err(c, "no cat")
                return out
            t = h.cat.tracker
            if isinstance(d.get("on"), bool):
                t.enabled = d["on"]
                t.reset()
            if _num(d.get("dl")) or _num(d.get("ul")):
                t.update(str(d.get("name", "")), round(_num(d.get("dl")) or 0), round(_num(d.get("ul")) or 0),
                         _num(d.get("rr")) or 0.0)
            ok(c)
        elif c == "wx":
            g = h.wind
            ws, wg = _num(d.get("ws")), _num(d.get("wg"))
            if g is None:
                err(c, "no wind")
            elif getattr(g, "local", False):
                ok(c)                                   # manda el viento medido (red local)
            elif ws is None and wg is None:
                err(c, "ws")
            elif (ws or 0) < 0 or (ws or 0) > 150 or (wg or 0) > 150:
                err(c, "range")
            else:
                wd = d.get("wd", -1)
                age = d.get("age", 0)
                age = int(min(max(age, 0), 86400)) if isinstance(age, (int, float)) else 0
                src = d.get("src", "")
                if g.update(ws or 0.0, wg if wg is not None else -1.0, int(wd) if isinstance(wd, (int, float)) else -1,
                            age, src if isinstance(src, str) else ""):
                    r.stop()
                    r.park()
                ok(c)
        elif c == "track":                              # {"c":"track","t":"sun"|"moon"|"p2p"|"sat"|"off"[,"id":"ISS"]}
            t = sky.parse(d.get("t"))
            if t < 0:
                err(c, "t")
            elif t == sky.SAT and d.get("id") and (e := h.select_sat(str(d["id"]))):
                err(c, e)                                   # satelite desconocido
            elif t == sky.RADEC and (e := h.set_radec(d)):
                err(c, e)                                   # punto del cielo mal dado
            else:
                e = h.sky_start(t)
                ok(c) if not e else err(c, e)
        elif c == "tle":                                # descargar/actualizar el TLE (en segundo plano)
            e = h.update_tle()
            ok(c) if not e else err(c, e)
        elif c == "att":                                # actitud de la base (IMU / giroscopo): r escora, p cabeceo, y giro; zero = tara
            if d.get("zero"):
                e = h.att_zero()
            else:
                e = h.set_attitude(float(_num(d.get("r")) or 0.0), float(_num(d.get("p")) or 0.0), float(_num(d.get("y")) or 0.0))
            ok(c) if not e else err(c, e)
        elif c == "peer":                               # P2P: posicion de la otra estacion {lat,lon,alt} o {loc}
            if d.get("loc"):
                ll = geo.locator_to_latlon(str(d["loc"]))
                lat, lon = ll if ll else (None, None)
            else:
                lat, lon = _num(d.get("lat")), _num(d.get("lon"))
            if lat is None or lon is None:
                err(c, "lat/lon or loc")
            else:
                e = h.set_peer(float(lat), float(lon), float(_num(d.get("alt")) or 0.0))
                ok(c) if not e else err(c, e)
        elif c == "inmarsat":                           # calibracion: apuntar / fijar el cero con un satelite Inmarsat
            e, dd = h.point_inmarsat(_num(d.get("lon")), bool(d.get("fix")))
            ok(c, **dd) if not e else out.append(dumps({"e": "err", "c": c, "m": e, **dd}))
        elif c == "reboot":
            ok(c)
            h.reboot()
        elif c == "update":                             # actualizar el software desde git y reinstalar (solo Raspberry Pi)
            e = h.self_update()
            ok(c) if not e else err(c, e)
        else:
            err(c, "unknown")
        return out

    @staticmethod
    def _catset(d: dict, h: ApiHost, ok, err):
        c = "catset"
        if not (h.cat and h.cat.rig):
            err(c, "no cat")
            return
        g = h.cat.rig
        good = True
        f = _num(d.get("f"))
        if "f" in d:
            good &= f is not None and 0 < f < 1e11 and g.set_freq(round(f))
        if isinstance(d.get("m"), str):
            good &= g.set_mode(d["m"])
        f = _num(d.get("txf"))
        if "txf" in d:
            good &= f is not None and 0 < f < 1e11 and g.set_tx_freq(round(f))
        if isinstance(d.get("split"), bool):
            good &= g.set_split(d["split"])
        if "tone" in d:
            t = _num(d.get("tone"))
            good &= t is not None and 0 <= t <= 260 and g.set_tone(round(t * 10))
        if isinstance(d.get("vfo"), str):
            good &= g.toggle_vfo() if d["vfo"] == "swap" else g.set_vfo("B" if d["vfo"] == "B" else "A")
        if isinstance(d.get("ptt"), bool):
            if d["ptt"] and not h.cat.ptt_allowed():
                err(c, "ptt not allowed")
                return
            good &= g.set_ptt(d["ptt"])
        ok(c) if good else err(c, "rejected")

    def tick(self, h: ApiHost) -> list[str]:
        if not self.sub_ms:
            return []
        now = time.monotonic()
        out = []
        if h.cat and h.cat.rig and now - self._last_cat >= max(1.0, self.sub_ms / 1000):   # estado de la emisora cada segundo
            self._last_cat = now
            out.append(dumps(h.cat.event()))
        moving = bool(h.rotor.status() & (ST_MOVING | ST_BUSY))
        every = self.sub_ms if moving else max(self.sub_ms, 5000)
        if moving != self._was_moving or (now - self._last_evt) * 1000 >= every:
            self._was_moving = moving
            self._last_evt = now
            out.append(pos_event(h))
        return out
