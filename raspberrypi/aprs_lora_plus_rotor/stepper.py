# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Motores paso a paso en la propia Raspberry Pi: Waveshare Stepper Motor HAT (dos DRV8825, STEP / DIR / ENABLE).

Motor 1 = acimut, motor 2 = elevacion. Posicion por cuenta de pasos (lazo abierto): se guarda al parar y al salir, y un
final de carrera opcional por eje la fija (busqueda al arrancar). La orden "here" (Corregir desfase) pone la posicion actual.
Rampa trapezoidal en un hilo propio; los pulsos van por lgpio (python3-lgpio, todos los modelos, tambien la Pi 5).

Pines del HAT (BCM, wiki de Waveshare): motor 1 DIR 13, STEP 19, ENABLE 12, MODE 16 17 20; motor 2 DIR 24, STEP 18,
ENABLE 4, MODE 21 22 27. Los micropasos los fijan los DIP D0-D5 (de fabrica, 1/32); los pines MODE solo llegan al DRV8825
con las resistencias de 0 ohm del reverso (modePins). Placas Rev2.1 (desde 2021): ENABLE alto = motor activo.
El STEP del motor 2 es el GPIO18, el mismo que el PPS del Uputronics GPS: con los dos HAT hay que llevarlo a otro pin.
"""

from __future__ import annotations

import json
import logging
import math
import os
import threading
import time

from . import board
from .protocols import (MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT, MOVE_UP, ST_BUSY, ST_MOVING, ST_MOVING_AZ, ST_MOVING_DOWN,
                        ST_MOVING_EL, ST_MOVING_LEFT, ST_MOVING_RIGHT, ST_MOVING_UP, Rotor)

log = logging.getLogger("stepper")

STATE_PATH = "/var/lib/aprs-lora-plus-rotor/stepper.json"
PPS_GPIO = 18                      # time pulse del Uputronics GPS HAT
MICRO = {1: (0, 0, 0), 2: (1, 0, 0), 4: (0, 1, 0), 8: (1, 1, 0), 16: (0, 0, 1), 32: (1, 0, 1)}   # MODE0, MODE1, MODE2

AXIS_DEFAULTS = {
    "az": {"on": True, "dir": 13, "step": 19, "en": 12, "mode": [16, 17, 20], "invert": False, "enActive": 1},
    "el": {"on": True, "dir": 24, "step": 18, "en": 4, "mode": [21, 22, 27], "invert": False, "enActive": 1},
    # 3er eje (inclinacion / cross-level), de serie apagado. TMC2209 aparte del HAT (STEP 5, DIR 6, ENABLE 23, activo
    # BAJO -> enActive 0). Independiente de az/el: no entra en rotctld (Hamlib solo az/el); se manda por API / WebUI /
    # app (orden "axis"). En fijo se aparca al centro; en movil (vehiculo / barco) nivela con el balanceo. Topes minX/maxX.
    "x": {"on": False, "dir": 6, "step": 5, "en": 23, "mode": [-1, -1, -1], "invert": False, "enActive": 0},
}
for _k, _a in AXIS_DEFAULTS.items():
    # mecanica: pasos por vuelta del motor, micropasos (los de los DIP), vueltas del motor por vuelta de la antena,
    # velocidad (grados/s de la antena), aceleracion (grados/s2); hold = motor con corriente parado (calienta);
    # final de carrera: pin (-1 = no), donde esta (grados), hacia donde buscarlo (-1 / 1), nivel activo, pullup
    _a.update({"stepsRev": 200, "micro": 32, "gear": 1.0, "dps": 4.0, "accel": 8.0, "hold": False,
               "home": -1, "homeAt": 0.0, "homeDir": -1, "homeActive": 0, "homePullup": True})
DEFAULTS = {"chip": -1, "enActive": 1, "modePins": False, "homeOnStart": True, "minAz": 0.0, "maxAz": 360.0,
            "minEl": 0.0, "maxEl": 90.0, "minX": -41.25, "maxX": 41.25, **AXIS_DEFAULTS}


def validate(s: dict, gps_on: bool, max_steps: int | None = None) -> str:
    """max_steps: pasos por segundo de los dos motores juntos que da esta Pi (board.FAMILIES)."""
    if max_steps is None:
        max_steps = int(board.info()["steps"])
    used: dict[int, str] = {}
    total = 0.0
    for ax in ("az", "el", "x"):
        a = s[ax]
        if not a["on"]:
            continue
        for k in ("dir", "step", "en"):
            p = int(a[k])
            if not 0 <= p <= 27:
                return f"stepper.{ax}.{k} 0-27"
            if p in used:
                return f"stepper.{ax}.{k} = {used[p]}"
            used[p] = f"{ax}.{k}"
        if int(a["home"]) >= 0:
            if not 0 <= int(a["home"]) <= 27 or int(a["home"]) in used:
                return f"stepper.{ax}.home"
            used[int(a["home"])] = f"{ax}.home"
        if int(a["micro"]) not in MICRO:
            return f"stepper.{ax}.micro 1 2 4 8 16 32"
        if int(a.get("enActive", s["enActive"])) not in (0, 1):
            return f"stepper.{ax}.enActive 0 / 1"
        if s["modePins"] and len(a["mode"]) != 3:
            return f"stepper.{ax}.mode [m0, m1, m2]"
        if not (int(a["stepsRev"]) > 0 and float(a["gear"]) > 0 and 0.05 <= float(a["dps"]) <= 90
                and 0.1 <= float(a["accel"]) <= 500 and int(a["homeDir"]) in (-1, 1)):
            return f"stepper.{ax}: stepsRev > 0, gear > 0, dps 0.05-90, accel 0.1-500, homeDir -1 / 1"
        total += StepperAxis.rate(a)
    if total > max_steps:
        return f"stepper: {total:.0f} pasos/s entre los dos motores; esta Pi da {max_steps} (baja dps o micro)"
    if gps_on and PPS_GPIO in used:
        return f"stepper.{used[PPS_GPIO]} = GPIO18, el PPS del GPS (docs/RASPBERRY_HATS.md)"
    if not (float(s["minAz"]) < float(s["maxAz"]) and float(s["maxAz"]) - float(s["minAz"]) <= 720
            and float(s["minEl"]) < float(s["maxEl"])):
        return "stepper: minAz < maxAz (hasta 720), minEl < maxEl"
    if s["x"]["on"] and not float(s["minX"]) < float(s["maxX"]):
        return "stepper: minX < maxX"
    return ""


# ---- GPIO ---------------------------------------------------------------------------------------------------------------
def pick_chip(labels: dict[int, str], want: str) -> int:
    """gpiochip del conector: el de la etiqueta del modelo (pinctrl-rp1 en la 5...) o, si no, el primer pinctrl-."""
    for pref in (want, "pinctrl-"):
        for c in sorted(labels):
            if pref and labels[c].startswith(pref):
                return c
    return -1


class Gpio:
    """lgpio sobre /dev/gpiochipN: el del conector de 40 pines (pinctrl-bcm2835 en la 3 y la Zero 2 W, bcm2711 en la 4, rp1 en la 5)."""

    def __init__(self, chip: int = -1, want: str = ""):
        import lgpio  # python3-lgpio
        self.lg = lgpio
        self.h = None
        if chip >= 0:
            self.h = lgpio.gpiochip_open(chip)
            log.info("GPIO: gpiochip%d", chip)
            return
        labels = {}
        for c in range(0, 16):                 # la Pi 5 con nucleos antiguos: gpiochip4; con los nuevos, gpiochip0
            try:
                h = lgpio.gpiochip_open(c)
            except Exception:  # noqa: BLE001
                continue
            try:
                labels[c] = str(lgpio.gpio_get_chip_info(h)[3])
            except Exception:  # noqa: BLE001
                labels[c] = ""
            lgpio.gpiochip_close(h)
        c = pick_chip(labels, want or board.info()["gpio"])
        if c >= 0:
            self.h = lgpio.gpiochip_open(c)
            log.info("GPIO: gpiochip%d (%s)", c, labels[c])
        if self.h is None:
            raise OSError("no hay gpiochip del conector de 40 pines")

    def output(self, pin: int, level: int):
        self.lg.gpio_claim_output(self.h, pin, level)

    def input(self, pin: int, pullup: bool):
        self.lg.gpio_claim_input(self.h, pin, self.lg.SET_PULL_UP if pullup else 0)

    def write(self, pin: int, level: int):
        self.lg.gpio_write(self.h, pin, level)

    def read(self, pin: int) -> int:
        return self.lg.gpio_read(self.h, pin)

    def close(self):
        try:
            self.lg.gpiochip_close(self.h)
        except Exception:  # noqa: BLE001
            pass


class FakeGpio:
    """Para las pruebas en el PC: cuenta los flancos de subida de cada pin."""

    def __init__(self):
        self.level: dict[int, int] = {}
        self.rises: dict[int, int] = {}
        self.inputs: dict[int, int] = {}

    def output(self, pin, level):
        self.level[pin] = level

    def input(self, pin, pullup):
        self.inputs.setdefault(pin, 1 if pullup else 0)

    def write(self, pin, level):
        if level and not self.level.get(pin):
            self.rises[pin] = self.rises.get(pin, 0) + 1
        self.level[pin] = level

    def read(self, pin):
        return self.inputs.get(pin, 0)

    def close(self):
        pass


# ---- un eje -------------------------------------------------------------------------------------------------------------
class StepperAxis:
    def __init__(self, name: str, c: dict, gpio, en_active: int, mode_pins: bool, lo: float, hi: float, wrap: bool):
        self.name, self.c, self.g = name, c, gpio
        self.en_active = 1 if en_active else 0
        self.spd = self.steps_per_deg(c)
        self.lo, self.hi, self.wrap = lo, hi, wrap
        self.pos = 0                          # pasos
        self.target: int | None = None        # pasos
        self.v = 0.0                          # pasos/s con signo
        self.vmax = float(c["dps"]) * self.spd
        self.acc = float(c["accel"]) * self.spd
        self.vmin = min(self.vmax, max(20.0, math.sqrt(self.acc)))   # primer paso a ~1/sqrt(a) s
        self.next_t = 0.0
        self.dirlvl = -1
        self.enabled = False
        self.idle_since = 0.0                 # reloj de step_once (perf_counter)
        self.home_pin = int(c["home"])
        gpio.output(int(c["step"]), 0)
        gpio.output(int(c["dir"]), 0)
        gpio.output(int(c["en"]), 1 - self.en_active)                  # sin corriente hasta moverse
        if mode_pins:
            for p, b in zip(c["mode"], MICRO[int(c["micro"])]):
                if int(p) >= 0:
                    gpio.output(int(p), b)
        if self.home_pin >= 0:
            gpio.input(self.home_pin, bool(c["homePullup"]))

    @staticmethod
    def steps_per_deg(c: dict) -> float:
        return int(c["stepsRev"]) * int(c["micro"]) * float(c["gear"]) / 360.0

    @staticmethod
    def rate(c: dict) -> float:
        return float(c["dps"]) * StepperAxis.steps_per_deg(c)

    @property
    def deg(self) -> float:
        return self.pos / self.spd

    def set_deg(self, d: float):
        self.pos = int(round(d * self.spd))

    def goto_deg(self, d: float):
        d = min(self.hi, max(self.lo, d))
        self.target = int(round(d * self.spd))

    def home_active(self) -> bool:
        return self.home_pin >= 0 and self.g.read(self.home_pin) == int(self.c["homeActive"])

    def enable(self, on: bool):
        if on != self.enabled:
            self.enabled = on
            self.g.write(int(self.c["en"]), self.en_active if on else 1 - self.en_active)

    def stop(self):
        """Parada con rampa: el objetivo pasa a donde se puede frenar."""
        if self.v:
            s = int(self.v * self.v / (2 * self.acc)) + 1
            self.target = self.pos + (s if self.v > 0 else -s)
        else:
            self.target = None

    def moving(self) -> bool:
        return self.target is not None and (self.target != self.pos or self.v != 0)

    def plan(self) -> int:
        """Velocidad tras el paso que toca (rampa trapezoidal). Devuelve el sentido del paso: -1, 0 (parado) o 1."""
        if self.target is None:
            self.v = 0.0
            return 0
        d = self.target - self.pos
        if d == 0:                                        # llegado: la rampa ya ha frenado (no pasarse y volver)
            self.v = 0.0
            self.target = None
            return 0
        want = 1 if d > 0 else -1
        v = abs(self.v)
        same = self.v == 0 or (self.v > 0) == (want > 0)
        brake = v * v / (2 * self.acc)
        dt = 1.0 / max(v, self.vmin)
        if not same or abs(d) <= brake:
            v = max(self.vmin, v - self.acc * dt)          # frenar (o frenar para dar la vuelta)
            if not same and v <= self.vmin * 1.01:
                v, same = self.vmin, True
        else:
            v = min(self.vmax, max(self.vmin, v + self.acc * dt))
        sgn = want if same else (1 if self.v > 0 else -1)
        self.v = sgn * v
        return sgn

    def pulse(self, sgn: int):
        lvl = (1 if sgn > 0 else 0) ^ (1 if self.c["invert"] else 0)
        if lvl != self.dirlvl:
            self.g.write(int(self.c["dir"]), lvl)
            self.dirlvl = lvl
            _spin(5e-6)                          # DRV8825: 650 ns de DIR a STEP
        st = int(self.c["step"])
        self.g.write(st, 1)
        _spin(2e-6)                              # alto minimo 1,9 us
        self.g.write(st, 0)
        self.pos += sgn


def _spin(s: float):
    end = time.perf_counter() + s
    while time.perf_counter() < end:
        pass


# ---- el rotor -----------------------------------------------------------------------------------------------------------
class StepperRotor(Rotor):
    """Rotor con dos motores paso a paso. Misma interfaz que HamlibRotor."""

    def __init__(self, cfg: dict, park: tuple[float, float] = (0.0, 0.0), gpio=None, state_path: str = STATE_PATH):
        self.cfg = cfg
        self.park_pos = park
        self.state_path = state_path
        self.g = gpio if gpio is not None else Gpio(int(cfg["chip"]))
        self.min_az, self.max_az = float(cfg["minAz"]), float(cfg["maxAz"])
        self.min_el, self.max_el = float(cfg["minEl"]), float(cfg["maxEl"])
        self.min_x, self.max_x = float(cfg["minX"]), float(cfg["maxX"])
        self.has_el = bool(cfg["el"]["on"])
        self.has_x = bool(cfg["x"]["on"])
        self.park_x = min(self.max_x, max(self.min_x, 0.0))      # inclinacion aparcada al centro
        wrap = self.max_az - self.min_az >= 360
        mk = lambda n, lo, hi, w: StepperAxis(n, cfg[n], self.g, int(cfg[n].get("enActive", cfg["enActive"])),
                                              bool(cfg["modePins"]), lo, hi, w)
        self.axes = {"az": mk("az", self.min_az, self.max_az, wrap)}
        if self.has_el:
            self.axes["el"] = mk("el", self.min_el, self.max_el, False)
        if self.has_x:
            self.axes["x"] = mk("x", self.min_x, self.max_x, False)
        self.taz: float | None = None
        self.tel: float | None = None
        self.manual = 0
        self.fault = ""
        self.known = self._load()
        self.homing: dict[str, float] = {}           # eje -> grados recorridos buscando el final de carrera
        self._lock = threading.Lock()
        self._wake = threading.Event()
        self._quit = False
        self._saved_at = 0.0
        self._thread = threading.Thread(target=self._loop, name="stepper", daemon=True)

    # ---- estado guardado ------------------------------------------------------------------------------------------------
    def _load(self) -> bool:
        try:
            with open(self.state_path, encoding="utf-8") as f:
                d = json.load(f)
            for n, a in self.axes.items():
                if n in d:
                    a.set_deg(float(d[n]))
            return True
        except (OSError, ValueError, TypeError):
            return False

    def save(self):
        d = {n: round(a.deg, 3) for n, a in self.axes.items()}
        try:
            os.makedirs(os.path.dirname(self.state_path) or ".", exist_ok=True)
            tmp = self.state_path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(d, f)
            os.replace(tmp, self.state_path)
        except OSError as e:
            log.warning("no se pudo guardar la posicion: %s", e)

    # ---- interfaz Rotor -------------------------------------------------------------------------------------------------
    def _az_target(self, az: float) -> float:
        """El acimut pedido dentro de los topes, por el camino mas corto desde donde esta (solape incluido)."""
        cur = self.axes["az"].deg
        best = None
        for k in range(-2, 3):
            c = az % 360.0 + 360.0 * k
            if self.min_az - 1e-6 <= c <= self.max_az + 1e-6 and (best is None or abs(c - cur) < abs(best - cur)):
                best = c
        return best if best is not None else min(self.max_az, max(self.min_az, az))

    def set_pos(self, az: float, el: float) -> bool:
        if self.homing:
            return False
        with self._lock:
            self.manual = 0
            t = self._az_target(az)
            self.taz = az % 360.0
            self.axes["az"].goto_deg(t)
            if self.has_el:
                self.tel = min(self.max_el, max(self.min_el, el))
                self.axes["el"].goto_deg(self.tel)
        self._wake.set()
        return True

    def set_az(self, az: float) -> bool:
        return self.set_pos(az, self.get_pos()[1])

    def get_pos(self) -> tuple[float, float]:
        az = self.axes["az"].deg % 360.0
        return az, (self.axes["el"].deg if self.has_el else 0.0)

    def get_axis(self, name: str) -> float | None:
        a = self.axes.get(name)
        return None if a is None else a.deg

    def set_axis(self, name: str, deg: float) -> bool:
        """Posiciona un eje auxiliar (p. ej. 'x' = inclinacion / cross-level). az y el van por set_pos."""
        a = self.axes.get(name)
        if a is None or name in ("az", "el") or self.homing:
            return False
        with self._lock:
            a.goto_deg(deg)
        self._wake.set()
        return True

    def stop(self) -> bool:
        with self._lock:
            self.taz = self.tel = None
            self.manual = 0
            self.homing.clear()
            for a in self.axes.values():
                a.stop()
        self._wake.set()
        return True

    def park(self) -> bool:
        ok = self.set_pos(*self.park_pos)
        if self.has_x:
            self.set_axis("x", self.park_x)
        return ok

    def move(self, direction: int, speed: int) -> bool:
        if self.homing or direction not in (MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT):
            return False
        if direction in (MOVE_UP, MOVE_DOWN) and not self.has_el:
            return False
        with self._lock:
            self.taz = self.tel = None
            self.manual = direction
            if direction in (MOVE_LEFT, MOVE_RIGHT):
                a = self.axes["az"]
                a.goto_deg(a.hi if direction == MOVE_RIGHT else a.lo)       # a mano = hasta el tope
            else:
                a = self.axes["el"]
                a.goto_deg(a.hi if direction == MOVE_UP else a.lo)
        self._wake.set()
        return True

    def here(self, axis: str, deg: float) -> str:
        """Corregir desfase: la posicion actual pasa a valer deg."""
        a = self.axes.get(axis)
        if a is None:
            return "axis"
        if a.moving():
            return "moving"
        with self._lock:
            if axis == "az":
                a.set_deg(self._az_target(deg))                          # la vuelta dentro de los topes mas cercana
            else:
                a.set_deg(deg)
            self.known = True
            self.fault = ""
        self.save()
        return ""

    def home(self, axes=("az", "el", "x")) -> str:
        """Buscar el final de carrera de los ejes que lo tienen."""
        todo = [n for n in axes if n in self.axes and self.axes[n].home_pin >= 0]
        if not todo:
            return "no home switch"
        with self._lock:
            self.taz = self.tel = None
            self.manual = 0
            self.fault = ""
            for n in todo:
                a = self.axes[n]
                span = (a.hi - a.lo) + 20.0
                a.target = a.pos + int(round(span * a.spd)) * int(a.c["homeDir"])
                self.homing[n] = span
        self._wake.set()
        return ""

    def status(self) -> int:
        s = 0
        az = self.axes["az"]
        if az.moving():
            s |= ST_BUSY | ST_MOVING | ST_MOVING_AZ | (ST_MOVING_RIGHT if az.v > 0 else ST_MOVING_LEFT)
        el = self.axes.get("el")
        if el is not None and el.moving():
            s |= ST_BUSY | ST_MOVING | ST_MOVING_EL | (ST_MOVING_UP if el.v > 0 else ST_MOVING_DOWN)
        x = self.axes.get("x")
        if x is not None and x.moving():
            s |= ST_BUSY | ST_MOVING
        return s

    def info(self) -> str:
        return "APRS LoRa+ Rotor (stepper motors, Waveshare Stepper Motor HAT" + (" + TMC2209 X" if self.has_x else "") + ")"

    @property
    def link_ok(self) -> bool:
        return not self.fault

    def raw(self) -> dict:
        r = {"known": self.known, "homing": bool(self.homing)}
        for n, a in self.axes.items():
            r[n + "Steps"] = a.pos
            r[n + "Deg"] = round(a.deg, 2)
            r[n + "StepsDeg"] = round(a.spd, 3)
            if a.home_pin >= 0:
                r[n + "Home"] = a.home_active()
        if self.fault:
            r["fault"] = self.fault
        return r

    def clear(self):
        self.fault = ""

    # ---- hilo de pasos --------------------------------------------------------------------------------------------------
    def start(self):
        if self.cfg["homeOnStart"] and any(a.home_pin >= 0 for a in self.axes.values()):
            self.home()
        self._thread.start()

    async def run(self):
        self.start()

    def close(self):
        self._quit = True
        self.stop()
        self._wake.set()
        if self._thread.is_alive():
            self._thread.join(3.0)
        for a in self.axes.values():
            a.enable(False)
        self.save()
        self.g.close()

    def _check_home(self, n: str, a: StepperAxis):
        if n not in self.homing:
            return
        if a.home_active():
            a.target = None
            a.v = 0.0
            a.set_deg(float(a.c["homeAt"]))
            del self.homing[n]
            self.known = True
            log.info("final de carrera %s: %.1f", n, a.deg)
        elif a.target is None or a.target == a.pos:
            del self.homing[n]
            self.fault = "HOME " + n.upper()
            log.warning("final de carrera de %s no encontrado", n)

    def step_once(self, now: float) -> float | None:
        """Da los pasos que tocan en 'now'. Devuelve cuando toca el siguiente (None = todo parado). Lo usan el hilo y las pruebas."""
        nxt = None
        with self._lock:
            for n, a in self.axes.items():
                if a.target is None and a.v == 0:
                    if a.enabled and not a.c["hold"] and now - a.idle_since > 0.5:
                        a.enable(False)
                    continue
                if a.next_t <= now:
                    self._check_home(n, a)
                    sgn = a.plan()
                    if sgn:
                        if not a.enabled:
                            a.enable(True)
                            a.v = 0.0
                            a.next_t = now + 0.002          # el DRV8825 tarda ~1,7 ms tras activarse
                            nxt = a.next_t if nxt is None else min(nxt, a.next_t)
                            continue
                        lo_s, hi_s = int(round(a.lo * a.spd)) - (0 if n not in self.homing else 1 << 30), \
                            int(round(a.hi * a.spd)) + (0 if n not in self.homing else 1 << 30)
                        if lo_s <= a.pos + sgn <= hi_s:
                            a.pulse(sgn)
                        else:
                            a.target, a.v = None, 0.0     # nunca mas alla de los topes
                        a.next_t = max(a.next_t, now - 0.01) + 1.0 / max(abs(a.v), a.vmin)
                    else:
                        a.idle_since = now
                        if self.manual and n == ("az" if self.manual in (MOVE_LEFT, MOVE_RIGHT) else "el"):
                            self.manual = 0
                        self._saved_at = 0.0
                if a.target is not None or a.v:
                    nxt = a.next_t if nxt is None else min(nxt, a.next_t)
            idle = all(a.target is None and a.v == 0 for a in self.axes.values())
            if idle and self._saved_at == 0.0:
                self._saved_at = now
                self.save()
        return nxt

    def _loop(self):
        while not self._quit:
            now = time.perf_counter()
            nxt = self.step_once(now)
            if nxt is None:
                self._wake.wait(0.2)
                self._wake.clear()
                continue
            wait = nxt - time.perf_counter()
            if wait > 0.002:
                self._wake.wait(wait - 0.001)
                self._wake.clear()
            while time.perf_counter() < nxt and not self._wake.is_set():
                pass
