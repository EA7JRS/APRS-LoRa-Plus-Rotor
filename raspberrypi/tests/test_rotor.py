# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pruebas del demonio (sin Raspberry ni rotor):  cd raspberrypi && python3 -m unittest discover -s tests -v"""

import asyncio
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from aprs_lora_plus_rotor import antennas, api, aprs, config, geo, kiss, protocols as P, sky  # noqa: E402
from aprs_lora_plus_rotor import cat as C  # noqa: E402
from aprs_lora_plus_rotor.hamlib import HamlibRotor  # noqa: E402
from aprs_lora_plus_rotor.web import load_page  # noqa: E402
from aprs_lora_plus_rotor.wind import WindGuard, WindRotor  # noqa: E402
from aprs_lora_plus_rotor import lan as L  # noqa: E402
from aprs_lora_plus_rotor.heading import HeadingRotor, ang_diff  # noqa: E402
from aprs_lora_plus_rotor import astro, sky, wmm  # noqa: E402
from aprs_lora_plus_rotor import stepper as ST, gnss, board  # noqa: E402


class FakeRotor(P.Rotor):
    def __init__(self):
        self.az, self.el = 10.0, 20.0
        self.min_az, self.max_az, self.min_el, self.max_el = 0.0, 450.0, 0.0, 180.0
        self.last_move = None
        self.stopped = self.parked = False

    def set_pos(self, az, el):
        self.az, self.el = az, el
        return True

    def get_pos(self):
        return self.az, self.el

    def stop(self):
        self.stopped = True
        return True

    def park(self):
        self.parked = True
        return True

    def move(self, d, s):
        self.last_move = (d, s)
        return True

    def status(self):
        return P.ST_MOVING | P.ST_MOVING_AZ

    def info(self):
        return "fake"


class TestProtocols(unittest.TestCase):
    def test_rotctld(self):
        r, s = FakeRotor(), P.RotctlSession()
        out, _ = s.feed(b"\\dump_state\n", r)
        self.assertEqual(out, b"1\n2\nmin_az=0.000000\nmax_az=450.000000\nmin_el=0.000000\nmax_el=180.000000\n"
                              b"south_zero=0\nrot_type=AzEl\ndone\n")
        self.assertEqual(s.feed(b"P 123.5 45\n", r)[0], b"RPRT 0\n")
        self.assertEqual(s.feed(b"p\n", r)[0], b"123.500000\n45.000000\n")
        self.assertEqual(s.feed(b"P -10 5\n", r)[0], b"RPRT 0\n")
        self.assertAlmostEqual(r.az, 350)
        self.assertEqual(s.feed(b"P x 5\n", r)[0], b"RPRT -1\n")
        self.assertEqual(s.feed(b"M 16 50\n", r)[0], b"RPRT 0\n")
        self.assertEqual(r.last_move, (P.MOVE_RIGHT, 50))
        self.assertEqual(s.feed(b"s\n", r)[0], b"MOVING MOVING_AZ \n")
        self.assertEqual(s.feed(b"P 1", r)[0], b"")
        self.assertEqual(s.feed(b"0 20\r\n", r)[0], b"RPRT 0\n")
        self.assertEqual(s.feed(b"q\n", r), (b"RPRT 0\n", True))

    def test_gs232(self):
        r = FakeRotor()
        r.az, r.el = 123, 45
        b, a = P.GS232Server(True), P.GS232Server(False)
        self.assertEqual(b.feed(b"C2\r", r), b"AZ=123  EL=045\r\n")
        self.assertEqual(a.feed(b"C2\r", r), b"+0123+0045\r\n")
        self.assertEqual(b.feed(b"W210 030\r", r), b"")
        self.assertEqual((r.az, r.el), (210, 30))
        self.assertEqual(b.feed(b"W999 030\r", r), b"?>\r\n")
        b.feed(b"X2\rR\r", r)
        self.assertEqual(r.last_move, (P.MOVE_RIGHT, 50))

    def test_easycomm(self):
        r, s = FakeRotor(), P.EasyCommServer()
        self.assertEqual(s.feed(b"AZ123.4 EL45.6 UP000 XXX DN000 XXX\n", r), b"")
        self.assertEqual(s.feed(b"AZ EL\n", r), b"AZ123.4 EL45.6\n")

    def test_rot2prog(self):
        f = P.rot2prog_set_frame(123.4, 45.0)
        self.assertEqual(f, b"\x574834\x0a4050\x0a\x2f\x20")
        r, s = FakeRotor(), P.Rot2ProgServer()
        out = s.feed(b"\x00" + f, r)
        self.assertAlmostEqual(r.az, 123.4)
        self.assertEqual(len(out), 12)
        az, el = P.rot2prog_parse_reply(out)
        self.assertAlmostEqual(az, 123.4)
        self.assertAlmostEqual(el, 45.0)


class TestAprs(unittest.TestCase):
    def test_totp_rfc6238(self):
        key = aprs.b32key("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ")
        self.assertEqual(key, b"12345678901234567890")
        self.assertEqual(aprs.hotp(key, 59 // 30, 8), 94287082)
        self.assertEqual(aprs.hotp(key, 1111111109 // 30, 8), 7081804)
        self.assertEqual(aprs.hotp(key, 2000000000 // 30, 8), 69279037)

    def test_control(self):
        c = aprs.AprsControl(aprs.AprsConfig(callsign="EA7JRS-12"))
        r = FakeRotor()
        mine, tx = c.on_frame("EA7JRS-9>APLRT1::EA7JRS-12:P 123 45{17", 1000, 0, r)
        self.assertTrue(mine)
        self.assertEqual(tx[0], "EA7JRS-12>APZLRR::EA7JRS-9 :ack17")
        self.assertIn("GO AZ 123 EL 45", tx[1])
        r.az = 0
        _, tx = c.on_frame("EA7JRS-9>APLRT1::EA7JRS-12:P 123 45{17", 1005, 0, r)
        self.assertEqual(len(tx), 1)
        self.assertEqual(r.az, 0)
        _, tx = c.on_frame("EA1AAA>APLRT1::EA7JRS-12:P 1 1{5", 1010, 0, r)
        self.assertIn(":rej5", tx[0])
        c.cfg.totp = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"
        now = 1762500000
        code = "%06d" % aprs.hotp(b"12345678901234567890", now // 30)
        _, tx = c.on_frame(f"EA7JRS-9>APLRT1::EA7JRS-12:P 10 20 {code}{{9", 1020, now, r)
        self.assertIn("ack9", tx[0])
        self.assertEqual(r.az, 10)
        _, tx = c.on_frame(f"EA7JRS-9>APLRT1::EA7JRS-12:P 10 20 {code}{{10", 1030, now, r)
        self.assertIn("rej10", tx[0])


class TestKiss(unittest.TestCase):
    def test_roundtrip(self):
        f = "EA7JRS-12>APZLRR,WIDE1-1::EA7JRS-9 :ack17"
        ax = kiss.tnc2_to_ax25(f)
        frame = kiss.kiss_encode(ax + b"\xc0\xdb")
        d = kiss.KissDecoder()
        got = d.feed(frame[:5]) + d.feed(frame[5:])
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0][:-2], ax)
        self.assertEqual(got[0][-2:], b"\xc0\xdb")
        self.assertEqual(kiss.ax25_to_tnc2(ax), f)
        self.assertEqual(kiss.ax25_to_tnc2(kiss.tnc2_to_ax25("EA7JRS-9>APLRT1,EA1AAA*,WIDE2-1:>hola")),
                         "EA7JRS-9>APLRT1,EA1AAA*,WIDE2-1:>hola")


class Host(api.ApiHost):
    def __init__(self):
        self.rotor = FakeRotor()
        self.cfg = config.Config(os.path.join(tempfile.mkdtemp(), "c.json"))

    def config_json(self):
        return self.cfg.to_json(True)

    def apply_config(self, text):
        return self.cfg.apply_json(text)

    def pin(self):
        return self.cfg.data["pin"]


class TestApiConfig(unittest.TestCase):
    def test_api(self):
        h, s = Host(), api.ApiSession()
        out = s.handle('{"c":"info"}', h)
        self.assertIn('"maxAz":450', out[0])
        self.assertIn("auth required", s.handle('{"c":"pos","az":1}', h)[0])
        self.assertIn("bad pin", s.handle('{"c":"auth","pin":"1"}', h)[0])
        self.assertEqual(s.handle('{"c":"auth","pin":"123456"}', h)[0], '{"e":"ok","c":"auth"}')
        s.handle('{"c":"pos","az":250.5,"el":300}', h)
        self.assertEqual((h.rotor.az, h.rotor.el), (250.5, 180))
        self.assertIn('"mv":true', s.handle('{"c":"get"}', h)[0])
        # Configuracion por trozos, cambiando el nombre
        pieces = s.handle('{"c":"cfgget"}', h)
        text = "".join(json.loads(p)["d"] for p in pieces)
        d = json.loads(text)
        d["name"] = "Rotor-Casa"
        t = json.dumps(d)
        n = (len(t) + 179) // 180
        last = None
        for i in range(n):
            last = s.handle(json.dumps({"c": "cfgset", "i": i, "n": n, "d": t[i * 180:(i + 1) * 180]}), h)
        self.assertIn('"done":true', last[0])
        self.assertEqual(h.cfg.data["name"], "Rotor-Casa")
        self.assertTrue(os.path.exists(h.cfg.path))
        # Secretos ocultos y validacion
        self.assertIn(config.MASK, h.cfg.to_json(False))
        self.assertEqual(h.cfg.apply_json('{"pin":"********","name":""}'), "name 1-24")
        self.assertEqual(h.cfg.apply_json('{"pin":"********"}'), "")
        self.assertEqual(h.cfg.data["pin"], "123456")

    def test_update_command(self):
        h, s = Host(), api.ApiSession()
        # Sin autenticar: rechazada
        self.assertIn("auth required", s.handle('{"c":"update"}', h)[0])
        s.handle('{"c":"auth","pin":"123456"}', h)
        # El Host base no soporta self_update -> error, no se lanza nada
        self.assertIn("not supported", s.handle('{"c":"update"}', h)[0])
        # Un host que la soporta -> ok
        launched = []

        class UpdHost(Host):
            def self_update(self):
                launched.append(True)
                return ""
        h2, s2 = UpdHost(), api.ApiSession()
        s2.handle('{"c":"auth","pin":"123456"}', h2)
        self.assertEqual(s2.handle('{"c":"update"}', h2)[0], '{"e":"ok","c":"update"}')
        self.assertEqual(launched, [True])


class WindHost(Host):
    def __init__(self):
        super().__init__()
        self.inner = FakeRotor()
        self.wind = WindGuard({"on": True, "park": 15, "resume": 10, "hold": 15})
        self.rotor = WindRotor(self.inner, self.wind)

    def pin(self):
        return ""

    def clear_fault(self):
        self.wind.release()


class TestWind(unittest.TestCase):
    def test_guard(self):
        g = WindGuard({"on": True, "park": 15, "resume": 10, "hold": 15})
        self.assertFalse(g.update(8, 12, 270, 60, "OWM", 0))
        self.assertTrue(g.update(9, 16.5, 270, 60, "OWM", 1))
        self.assertFalse(g.update(9, 17, 270, 60, "OWM", 2) or not g.locked)
        self.assertFalse(g.update(5, 8, 270, 0, "OWM", 2 + 300) or not g.locked)
        self.assertEqual(g.hold_left_s(2 + 300), 600)
        g.tick(2 + 900)
        self.assertFalse(g.locked)
        self.assertTrue(g.update(16, -1, -1, 0, "", 2 + 960))       # sin racha cuenta el medio
        g.release()
        self.assertFalse(g.locked)
        g.on = False
        self.assertFalse(g.update(30, 40, -1, 0, "", 2 + 1000) or g.locked)

    def test_api(self):
        h, s = WindHost(), api.ApiSession()
        self.assertIn('"wind":true', s.handle('{"c":"info"}', h)[0])
        self.assertEqual(s.handle('{"c":"wx","ws":20,"wg":28.4,"wd":250,"age":120,"src":"OWM"}', h)[0], '{"e":"ok","c":"wx"}')
        self.assertTrue(h.inner.parked and h.inner.stopped and h.wind.locked)
        self.assertIn("rejected", s.handle('{"c":"pos","az":100}', h)[0])
        self.assertIn("rejected", s.handle('{"c":"move","d":"cw"}', h)[0])
        w = json.loads(s.handle('{"c":"get"}', h)[0])["wind"]
        self.assertEqual((w["lk"], w["ws"], w["wg"], w["pk"], w["src"]), (True, 20, 28.4, 28.4, "OWM"))
        s.handle('{"c":"clear"}', h)
        self.assertEqual(s.handle('{"c":"pos","az":100}', h)[0], '{"e":"ok","c":"pos"}')
        self.assertIn("range", s.handle('{"c":"wx","ws":-3}', h)[0])
        self.assertIn("no wind", api.ApiSession().handle('{"c":"wx","ws":3}', type("H", (Host,), {"pin": lambda self: ""})())[0])
        self.assertEqual(config.validate({**config.DEFAULTS, "wind": {"on": True, "park": 15, "resume": 20, "hold": 5}})[:4], "wind")



def _nmea(body: str) -> str:
    c = 0
    for ch in body:
        c ^= ord(ch)
    return f"${body}*{c:02X}"


class TestHeading(unittest.TestCase):
    def test_compensation(self):
        f = FakeRotor()
        f.az = 0.0
        h = HeadingRotor(f, enabled=True)
        self.assertTrue(h.set_pos(120, 30) and f.az == 120)     # sin rumbo: tal cual
        h.set_heading(90, 1000)
        self.assertEqual((h.min_az, h.max_az), (0.0, 360.0))
        self.assertAlmostEqual(h.get_pos()[0], 210)
        self.assertTrue(h.set_pos(45, 10))
        self.assertAlmostEqual(f.az, 315)
        h.set_heading(92, 1000.5)
        self.assertAlmostEqual(f.az, 315)                        # 2 grados: no
        h.set_heading(95, 1000.8)
        self.assertAlmostEqual(f.az, 315)                        # aun no ha pasado 1 s
        h.set_heading(95, 1001.2)
        self.assertAlmostEqual(f.az, 310)
        f.az = 400.0
        h.set_heading(0, 1010)
        self.assertTrue(h.set_az(30))
        self.assertAlmostEqual(f.az, 390)                        # solapamiento: la vuelta mas cercana
        self.assertTrue(h.stop() and h.target is None)
        self.assertTrue(h.stale(1021) and not h.stale(1009))
        self.assertEqual(h.info_event(True, 1012), {"on": True, "v": 0.0, "age": 2})
        self.assertAlmostEqual(ang_diff(350, 10), 20)
        # detras: aparcado por viento rechaza y deja de seguir
        g = WindGuard({"on": True, "park": 15, "resume": 10, "hold": 15})
        h2 = HeadingRotor(WindRotor(f, g), enabled=True)
        h2.set_heading(10, 0)
        g.update(20, 25, -1, 0, "lan")
        self.assertFalse(h2.set_pos(200, 0))
        self.assertIsNone(h2.target)


class _XRotor:
    """Rotor de pega con eje X (inclinacion) para probar la nivelacion cross-level."""
    def __init__(self):
        self.az = self.el = 0.0
        self.min_az, self.max_az, self.min_el, self.max_el = 0.0, 360.0, 0.0, 90.0
        self.has_x, self.min_x, self.max_x, self.x = True, -40.0, 40.0, 0.0
    def set_pos(self, az, el):
        self.az, self.el = az, el
        return True
    def set_az(self, az):
        self.az = az
        return True
    def set_axis(self, name, deg):
        if name == "x":
            self.x = deg
            return True
        return False
    def get_pos(self):
        return self.az, self.el
    def stop(self):
        return True
    def park(self):
        return True
    def move(self, d, s):
        return True
    def status(self):
        return 0


class TestCrossLevel(unittest.TestCase):
    def test_cross_level(self):
        from aprs_lora_plus_rotor.attitude import AttitudeRotor
        f = _XRotor()
        a = AttitudeRotor(f, enabled=True, level=True, x_sign=1)
        self.assertTrue(a.leveling)
        a.set_attitude(10.0, 0.0, 0.0)                         # escora 10°
        a.set_pos(100.0, 20.0)
        self.assertAlmostEqual(f.x, -10.0)                     # el eje X cancela la escora
        self.assertAlmostEqual(f.az, 100.0, places=3)          # residuo 0: el az/el es el nivelado
        self.assertAlmostEqual(f.el, 20.0, places=3)
        a.set_attitude(60.0, 0.0, 0.0)                         # escora mayor que el tope del eje (±40)
        a.set_pos(100.0, 20.0)
        self.assertAlmostEqual(f.x, -40.0)                     # el eje llega al tope; el az/el afina el resto
        self.assertTrue(a.info_event().get("lvl"))
        f.x = 5.0                                              # con level off el eje no se toca
        a.level = False
        self.assertFalse(a.leveling)
        a.set_pos(100.0, 20.0)
        self.assertEqual(f.x, 5.0)


class TestInmarsat(unittest.TestCase):
    def test_geostationary(self):
        az, el, dist = geo.geostationary_azel(40.0, 0.0, 0.0)       # mismo meridiano
        self.assertAlmostEqual(az, 180.0, places=1)                 # al sur
        self.assertAlmostEqual(el, 43.7, places=0)
        self.assertAlmostEqual(dist, 37500, delta=1500)
        az2, el2, _ = geo.geostationary_azel(40.0, 0.0, 30.0)       # satelite al este
        self.assertTrue(90.0 < az2 < 180.0)
        self.assertLess(el2, el)
        _, el3, _ = geo.geostationary_azel(40.0, 0.0, 150.0)        # muy al oeste: bajo el horizonte
        self.assertLess(el3, 0.0)

    def test_api_inmarsat(self):
        class H(api.ApiHost):
            rotor = None
            def point_inmarsat(s, lon, fix):
                return "", {"az": 180.0, "el": 43.0, "km": 37500, "lon": lon or 25.0, **({"fixed": True} if fix else {})}
        h, sess = H(), api.ApiSession()
        o = sess.handle('{"c":"inmarsat"}', h)[0]
        self.assertIn('"e":"ok"', o)
        self.assertIn('"az":180', o)
        self.assertIn('"fixed":true', sess.handle('{"c":"inmarsat","fix":true}', h)[0])

        class H2(api.ApiHost):
            rotor = None
            def point_inmarsat(s, lon, fix):
                return "no position", {}
        self.assertIn("no position", sess.handle('{"c":"inmarsat"}', H2())[0])


class TestSky(unittest.TestCase):
    def test_formulas(self):
        # Mismos valores que el plotter (pygeomag y PyEphem)
        for lat, lon, alt, year, dec, inc in ((36.53, -6.29, 0, 2026.86, -0.0318, 49.8584), (89, -121, 28, 2025.0, -99.7746, 88.4666),
                                              (-80, 240, 48, 2027.5, 68.2187, -72.0065), (0, 0, 0, 2026.0, -3.8929, -30.1940)):
            f = wmm.DEFAULT.field(lat, lon, alt, year)
            self.assertAlmostEqual(f["D"], dec, delta=0.002)
            self.assertAlmostEqual(f["I"], inc, delta=0.002)
        self.assertAlmostEqual(wmm.decimal_year(1767225600), 2026.0)
        cof = "    2030.0  WMM-2030  12/01/2029\n" + "".join(f"{n} {m} {'-30000' if n == 1 and m == 0 else '0'} 0 0 0\n"
                                                         for n in range(1, 13) for m in range(n + 1)) + "999999\n"
        w = wmm.Wmm.load_cof(cof)
        self.assertEqual((w.epoch, w.valid_until, w.name), (2030.0, 2035.0, "WMM-2030"))
        self.assertAlmostEqual(w.declination(0, 0, 2030), 0.0, 6)
        with self.assertRaises(ValueError):
            wmm.Wmm.load_cof("2030.0 X\n1 0 1 0 0 0\n")
        t = 1794490200                                           # 2026-11-12 13:30 UTC, Rota
        az, el = astro.sun_position(t, 36.53, -6.29)
        self.assertAlmostEqual(el, 32.434, delta=0.02); self.assertAlmostEqual(az, 202.895, delta=0.02)
        # celnav (como el plotter): almanaque nautico de 2026, 16 jun 9 h: Sol AHG 314 49,6 dec N 23 21,2; Aries 39 39,0;
        # Regulo (12 h) AHS 207 33,3 dec N 11 50,3
        from aprs_lora_plus_rotor import celnav
        jd = astro.julian_day(1781600400)                        # 2026-06-16 09:00 UTC
        gha, dec = celnav.sun_gha_dec(jd)
        self.assertAlmostEqual(gha, 314 + 49.6 / 60, delta=0.1 / 60); self.assertAlmostEqual(dec, 23 + 21.2 / 60, delta=0.1 / 60)
        self.assertAlmostEqual(celnav.aries_gha(jd), 39 + 39.0 / 60, delta=0.1 / 60)
        sha, dec = celnav.star_sha_dec(celnav.find_star("regulus"), jd + 3 / 24)
        self.assertAlmostEqual(sha, 207 + 33.3 / 60, delta=0.11 / 60); self.assertAlmostEqual(dec, 11 + 50.3 / 60, delta=0.11 / 60)
        self.assertEqual(len(celnav.STARS), 157)
        az, el, dist = astro.moon_position(t, 36.53, -6.29)
        self.assertAlmostEqual(el, 22.708, delta=0.03); self.assertAlmostEqual(az, 162.424, delta=0.03)
        self.assertTrue(400000 < dist < 410000)
        self.assertAlmostEqual(astro.refraction(0), 0.48, delta=0.03)
        self.assertEqual([astro.beaufort(k) for k in (0, 0.5, 3.4, 10, 38.9, 70)], [0, 1, 1, 3, 8, 12])
        # radioastronomia (linea H1): RA/Dec -> az/el y galacticas -> ecuatoriales
        az, el = astro.radec_to_azel(50.0, 90.0, 40.0, 0.0, t)       # polo celeste norte: el = lat, az = norte
        self.assertAlmostEqual(el, 40.0, delta=0.01)
        self.assertAlmostEqual(az % 360.0, 0.0, delta=0.01)
        ra, dec = astro.galactic_to_radec(0.0, 0.0)                   # centro galactico
        self.assertAlmostEqual(ra, 266.405, delta=0.1)
        self.assertAlmostEqual(dec, -28.936, delta=0.1)
        self.assertAlmostEqual(astro.galactic_to_radec(0.0, 90.0)[1], 27.128, delta=0.1)   # polo galactico norte

    def test_tracking(self):
        f = FakeRotor()
        s = sky.SkyRotor(f)
        t = 1794490200
        self.assertIn("position", s.start(sky.SUN, None, t, 0))
        self.assertIn("time", s.start(sky.SUN, (36.53, -6.29), 0, 0))
        self.assertEqual(s.start(sky.SUN, (36.53, -6.29), t, 10), "")
        self.assertTrue(s.tracking and s.aims == 1)
        self.assertAlmostEqual(f.az, 202.895, delta=0.03)
        s.tick(t + 2, 12.5); self.assertEqual(s.aims, 1)        # el sol apenas se ha movido
        s.tick(t + 240, 20); self.assertEqual(s.aims, 2)
        self.assertTrue(s.set_pos(10, 10) is not False and not s.tracking)
        f.az = 50.0
        self.assertEqual(s.start(sky.MOON, (36.53, -6.29), 1794517200, 100), "")
        self.assertTrue(s.below and f.az == 50.0)                # la luna esta bajo el horizonte: espera
        self.assertEqual(s.info_event()["t"], "moon")
        s.stop(); self.assertIsNone(s.info_event())
        self.assertEqual((sky.parse("sun"), sky.parse("moon"), sky.parse("off"), sky.parse("x"), sky.parse(3)), (1, 2, 0, -1, -1))
        # Por la API
        h = WindHost()
        h.sky = sky.SkyRotor(h.rotor)
        h.rotor = h.sky
        h.sky_start = lambda tg: h.sky.start(tg, (36.53, -6.29), t)
        ses = api.ApiSession()
        self.assertIn('"sky":true', ses.handle('{"c":"info"}', h)[0])
        self.assertEqual(ses.handle('{"c":"track","t":"moon"}', h)[0], '{"e":"ok","c":"track"}')
        self.assertIn('"trk":{"t":"moon","az":162.4', ses.handle('{"c":"get"}', h)[0])
        self.assertIn('"err"', ses.handle('{"c":"track","t":"mars"}', h)[0])
        ses.handle('{"c":"stop"}', h)
        self.assertNotIn("trk", ses.handle('{"c":"get"}', h)[0])
        # RADEC (radioastronomia): seguir un punto fijo del cielo
        g2 = FakeRotor()
        s3 = sky.SkyRotor(g2)
        s3.set_radec(123.0, 90.0, "pole")                        # polo celeste norte
        self.assertEqual(s3.start(sky.RADEC, (40.0, 0.0), t, 0), "")
        self.assertAlmostEqual(g2.az % 360.0, 0.0, delta=0.1)
        self.assertAlmostEqual(g2.el, 40.0, delta=0.1)
        ev = s3.info_event()
        self.assertEqual((ev["t"], ev["name"], ev["dec"]), ("radec", "pole", 90.0))

        class RH(api.ApiHost):
            def __init__(s2):
                s2.sky = sky.SkyRotor(FakeRotor())
                s2.rotor = s2.sky
            def set_radec(s2, dd):
                ra, dec = astro.galactic_to_radec(float(dd["l"]), float(dd["b"]))
                s2.sky.set_radec(ra, dec, "gal")
                return ""
            def sky_start(s2, tg):
                return s2.sky.start(tg, (40.0, 0.0), t)
        rh, rs = RH(), api.ApiSession()
        self.assertIn('"e":"ok"', rs.handle('{"c":"track","t":"radec","l":0,"b":0}', rh)[0])
        self.assertIn('"t":"radec"', rs.handle('{"c":"get"}', rh)[0])


class TestLan(unittest.TestCase):
    def test_nmea(self):
        st = L.Store()
        self.assertTrue(L.feed_nmea(_nmea("GPRMC,123519,A,3631.84,N,00617.02,W,5.2,247.0,121126,,"), st, 100))
        self.assertAlmostEqual(st.get("lat", 100), 36.530667, 5)
        self.assertAlmostEqual(st.get("lon", 100), -6.283667, 5)
        self.assertTrue(L.feed_nmea(_nmea("IIHDT,123.4,T"), st, 100))
        self.assertAlmostEqual(st.get("hdg", 100), 123.4)
        self.assertTrue(L.feed_nmea(_nmea("WIMWV,035.0,R,12.0,N,A"), st, 100))
        self.assertAlmostEqual(st.get("aws", 100), 12.0)
        self.assertTrue(L.feed_nmea(_nmea("WIMWV,270,T,10.0,M,A"), st, 100))
        self.assertAlmostEqual(st.get("tws", 100), 10.0 / L.KN_MS)
        self.assertFalse(L.feed_nmea(_nmea("WIMWV,035.0,R,12.0,N,V"), st, 100))     # dato no valido
        self.assertFalse(L.feed_nmea("$IIHDT,123.4,T*00", st, 100))                  # suma mal
        self.assertTrue(L.feed_nmea("\\s:catcher,c:1700000000*5A\\" + _nmea("IIHDG,100.0,,,,"), st, 100))
        self.assertAlmostEqual(st.get("hdgMag", 100), 100.0)
        self.assertIsNone(st.get("hdg", 111))                    # pasados 10 s ya no vale

    def test_signalk(self):
        st = L.Store()
        self.assertEqual(L.feed_signalk('{"name":"signalk-server","self":"vessels.urn:mrn:imo:mmsi:224000000"}', st), 0)
        d = {"context": "vessels.urn:mrn:imo:mmsi:224000000", "updates": [{"values": [
            {"path": "navigation.position", "value": {"latitude": 36.5, "longitude": -6.3}},
            {"path": "navigation.headingTrue", "value": 1.5707963},
            {"path": "environment.wind.speedApparent", "value": 7.5}]}]}
        self.assertEqual(L.feed_signalk(json.dumps(d), st), 3)
        self.assertAlmostEqual(st.get("hdg"), 90.0, 3)
        self.assertAlmostEqual(st.get("aws") * L.KN_MS, 7.5)
        d["context"] = "vessels.urn:mrn:imo:mmsi:999999999"      # otro barco
        self.assertEqual(L.feed_signalk(json.dumps(d), st), 0)
        self.assertEqual(L.feed_signalk("no json", st), 0)

    def test_owm_and_apply(self):
        st = L.Store()
        self.assertTrue(L.feed_owm({"wind": {"speed": 9.0, "deg": 250, "gust": 18.0}}, st, 0))
        self.assertIn("lat=36.5000", L.owm_url("k", 36.5, -6.3))
        g = WindGuard({"on": True, "park": 15, "resume": 10, "hold": 15})
        f = FakeRotor()
        h = HeadingRotor(f)
        cfg = dict(config.DEFAULTS["lan"], owmKey="k")
        lan = L.Lan(cfg, {"lat": 36.5, "lon": -6.3}, g, h)
        lan.store = st
        parked = []
        lan.on_park = lambda: parked.append(1)
        lan.apply(10)
        self.assertTrue(g.locked and parked and g.src == "owm" and not g.local)    # racha de 18 m/s de internet
        self.assertEqual(lan.position(10), (36.5, -6.3))         # sin posicion de la red: la configurada
        st.set("lat", 40.0, now=20); st.set("lon", -3.0, now=20); st.set("aws", 10.0, now=20)
        lan.apply(21)
        self.assertTrue(g.local and g.src == "lan")              # medido: la app ya no cuenta
        self.assertEqual(lan.position(21), (40.0, -3.0))
        api_host = WindHost()
        api_host.wind = g
        self.assertEqual(api.ApiSession().handle('{"c":"wx","ws":1}', api_host)[0], '{"e":"ok","c":"wx"}')
        st.set("hdgMag", 100.0, now=22)
        cfg["magVar"] = -1.5
        cfg["magAuto"] = False
        h.enabled = True
        lan.apply(22)
        self.assertAlmostEqual(h.heading, 98.5)
        cfg["magAuto"] = True                                    # declinacion del WMM en la posicion de la red
        lan.apply(22)
        self.assertAlmostEqual(h.heading, (100.0 + wmm.declination(40.0, -3.0)) % 360, 6)
        self.assertGreater(wmm.declination(40.0, -3.0), 0)       # centro de la Peninsula, 2026: ligeramente al este
        self.assertEqual(config.validate({**config.DEFAULTS, "lan": dict(cfg, kind=5)})[:8], "lan.kind")
        c = config.Config(os.path.join(tempfile.mkdtemp(), "c.json"))
        c.data["lan"]["owmKey"] = "secret"
        self.assertNotIn("secret", c.to_json(False))

class TestHamlibClient(unittest.TestCase):
    """HamlibRotor contra un rotctld simulado (nuestro propio servidor con un rotor de pruebas)."""

    def test_client(self):
        async def run():
            fake = FakeRotor()

            async def handler(reader, writer):
                sess = P.RotctlSession(model=603)
                while data := await reader.read(256):
                    out, close = sess.feed(data, fake)
                    writer.write(out)
                    await writer.drain()
                    if close:
                        break
                writer.close()

            srv = await asyncio.start_server(handler, "127.0.0.1", 0)
            port = srv.sockets[0].getsockname()[1]
            r = HamlibRotor({"port": port, "pollMs": 100})
            task = asyncio.create_task(r.run())
            for _ in range(50):
                await asyncio.sleep(0.05)
                if r.known:
                    break
            self.assertTrue(r.connected and r.known)
            self.assertEqual(r.max_az, 450)
            self.assertEqual(r.get_pos(), (10.0, 20.0))
            r.set_pos(200, 30)
            await asyncio.sleep(0.3)
            self.assertEqual((fake.az, fake.el), (200.0, 30.0))
            self.assertEqual(r.get_pos(), (200.0, 30.0))
            r.move(P.MOVE_LEFT, 0)
            await asyncio.sleep(0.2)
            self.assertEqual(fake.last_move, (P.MOVE_LEFT, 0))
            r.stop()
            await asyncio.sleep(0.2)
            self.assertTrue(fake.stopped)
            task.cancel()
            srv.close()
        asyncio.run(run())


class FakeRig(C.Rig):
    name = "Prueba"

    def __init__(self):
        super().__init__()
        self.st.link = True
        self.st.freq, self.st.mode, self.st.smeter = 145825000, "FM", 5

    def set_freq(self, hz):
        self.st.freq = hz
        return True

    def set_mode(self, m):
        self.st.mode = C.norm_mode(m)
        return True

    def set_tx_freq(self, hz):
        self.st.tx_freq = hz
        return True

    def set_split(self, on):
        self.st.split = on
        return True

    def toggle_vfo(self):
        self.st.vfo = "B" if self.st.vfo == "A" else "A"
        return True

    def _ptt(self, on):
        self.st.ptt = on
        return True

    def set_tone(self, dhz):
        self.st.tone = dhz
        return True


class TestCat(unittest.TestCase):
    def test_helpers(self):
        self.assertEqual(C.norm_mode("DATA-U"), "PKTUSB")
        self.assertEqual(C.norm_mode("nfm"), "FM")
        self.assertEqual(C.smeter_db(9), 0)
        self.assertEqual(C.smeter_db(0), -54)
        self.assertEqual(C.smeter_db(15), 60)
        self.assertEqual(C.smeter_from_db(-54), 0)
        self.assertEqual(C.smeter_from_db(20), 11)
        # 437 MHz alejándose a 5 km/s: baja ~7,3 kHz; la subida se corrige al revés
        self.assertEqual(C.doppler_rx(437_000_000, 5.0), 437_000_000 - 7288)
        self.assertEqual(C.doppler_tx(145_900_000, 5.0), 145_900_000 + 2433)

    def test_rigctld_server(self):
        rig, s = FakeRig(), C.RigctlSession()
        out, close = s.feed(b"f\nF 144300000\nf\nm\nM USB 0\nm\n", rig)
        self.assertEqual(out.decode(), "145825000\nRPRT 0\n144300000\nFM\n15000\nRPRT 0\nUSB\n2400\n")
        self.assertFalse(close)
        self.assertEqual(s.feed(b"T 1\n", rig)[0], b"RPRT -9\n")          # PTT no permitido
        self.assertFalse(rig.st.ptt)
        rig.allow_ptt = True
        self.assertEqual(s.feed(b"\\set_ptt 1\nt\n", rig)[0], b"RPRT 0\n1\n")
        self.assertEqual(s.feed(b"l STRENGTH\nV VFOB\nv\nI 435000000\ni\n", rig)[0],
                         b"-24\nRPRT 0\nVFOB\nRPRT 0\n435000000\n")
        ds = s.feed(b"\\dump_state\n", rig)[0].decode().splitlines()
        self.assertEqual(ds[:3], ["0", "2", "2"])
        self.assertEqual(s.feed(b"C 670\nc\nU TONE 0\nu TONE\nU TONE 1\nc\nU NB 1\n", rig)[0],
                         b"RPRT 0\n670\nRPRT 0\n0\nRPRT 0\n670\nRPRT -4\n")
        self.assertEqual(s.feed(b"\\chk_vfo\nq\n", rig), (b"0\nRPRT 0\n", True))

    def test_rigctl_client(self):
        """Cliente contra un rigctld simulado (nuestro servidor con FakeRig)."""
        async def run():
            fake = FakeRig()

            async def handler(reader, writer):
                sess = C.RigctlSession()
                while data := await reader.read(256):
                    out, close = sess.feed(data, fake)
                    writer.write(out)
                    await writer.drain()
                    if close:
                        break
                writer.close()
            srv = await asyncio.start_server(handler, "127.0.0.1", 0)
            port = srv.sockets[0].getsockname()[1]
            rig = C.RigctlClientRig("127.0.0.1", port, "SDR++", 100)
            task = asyncio.create_task(rig.run())
            for _ in range(40):
                await asyncio.sleep(0.05)
                if rig.st.link:
                    break
            self.assertTrue(rig.st.link)
            self.assertEqual((rig.st.freq, rig.st.mode, rig.st.smeter), (145825000, "FM", 5))
            rig.set_freq(437800000)
            rig.set_mode("USB")
            rig.set_tone(670)
            self.assertFalse(rig.set_ptt(True))                             # no permitido
            await asyncio.sleep(0.3)
            self.assertEqual((fake.st.freq, fake.st.mode, fake.st.ptt, fake.st.tone), (437800000, "USB", False, 670))
            # Seguimiento con Doppler sobre el cliente
            tr = C.CatTracker(min_step=50, min_s=0)
            tr.update("ISS", 437800000, 0, -6.0, now=100.0)
            tr.step(rig, now=100.1)
            await asyncio.sleep(0.3)
            self.assertEqual(fake.st.freq, C.doppler_rx(437800000, -6.0))
            self.assertEqual(tr.doppler, C.doppler_rx(437800000, -6.0) - 437800000)
            tr.step(rig, now=200.0)                                          # sin datos: se apaga
            self.assertFalse(tr.active)
            task.cancel()
            srv.close()
        asyncio.run(run())

    def test_sdrconnect(self):
        """SdrConnectRig contra un servidor WebSocket simulado con la API de SDRconnect."""
        async def run():
            props = {"device_vfo_frequency": "7074000", "device_center_frequency": "7000000",
                     "device_sample_rate": "2000000", "demodulator": "USB", "signal_power": "-80.5"}
            got = []

            async def handler(reader, writer):
                await reader.readuntil(b"\r\n\r\n")
                writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                             b"Sec-WebSocket-Accept: x\r\n\r\n")
                try:
                    while True:
                        op, p = await C.ws_read(reader)
                        d = json.loads(p)
                        got.append(d)
                        if d["event_type"] == "set_property":
                            props[d["property"]] = d["value"]
                        elif d["event_type"] == "get_property":
                            r = json.dumps({"event_type": "get_property_response", "property": d["property"],
                                            "value": props.get(d["property"], "")}).encode()
                            writer.write(bytes([0x81, len(r)]) + r if len(r) < 126 else
                                         bytes([0x81, 126]) + len(r).to_bytes(2, "big") + r)
                            await writer.drain()
                except (asyncio.IncompleteReadError, ConnectionError):
                    pass
            srv = await asyncio.start_server(handler, "127.0.0.1", 0)
            port = srv.sockets[0].getsockname()[1]
            rig = C.SdrConnectRig("127.0.0.1", port, "RSPdx-R2")
            rig.allow_ptt = True
            task = asyncio.create_task(rig.run())
            for _ in range(40):
                await asyncio.sleep(0.05)
                if rig.st.freq and rig.sample_rate:
                    break
            self.assertEqual((rig.st.freq, rig.st.mode, rig.center, rig.sample_rate), (7074000, "USB", 7000000, 2e6))
            self.assertEqual(rig.st.smeter, C.smeter_from_db(-80.5 + 73))
            self.assertFalse(rig.set_ptt(True))                              # receptor
            rig.set_freq(7200000)                                            # dentro de la banda: solo el VFO
            rig.set_mode("FM")
            await asyncio.sleep(0.2)
            self.assertEqual((props["device_vfo_frequency"], props["device_center_frequency"], props["demodulator"]),
                             ("7200000", "7000000", "NFM"))
            rig.set_freq(14074000)                                           # fuera: también el centro
            await asyncio.sleep(0.2)
            self.assertEqual((props["device_vfo_frequency"], props["device_center_frequency"]), ("14074000", "14074000"))
            self.assertTrue(all(d["device"] == "primary" for d in got))
            task.cancel()
            srv.close()
        asyncio.run(run())

    def test_api_and_config(self):
        h, s = Host(), api.ApiSession()
        self.assertIn("no cat", s.handle('{"c":"cat"}', h)[0])
        h.cat = C.Cat({"type": "none"})
        h.cat.rig = FakeRig()
        self.assertIn('"cat":true', s.handle('{"c":"info"}', h)[0])
        e = json.loads(s.handle('{"c":"cat"}', h)[0])
        self.assertEqual((e["f"], e["m"], e["pa"], e["tr"]["en"]), (145825000, "FM", False, True))
        self.assertIn("auth required", s.handle('{"c":"catset","f":1}', h)[0])
        s.handle('{"c":"auth","pin":"123456"}', h)
        self.assertEqual(s.handle('{"c":"catset","f":432100000,"m":"USB","vfo":"swap"}', h)[0], '{"e":"ok","c":"catset"}')
        self.assertEqual((h.cat.rig.st.freq, h.cat.rig.st.mode, h.cat.rig.st.vfo), (432100000, "USB", "B"))
        self.assertIn("ptt not allowed", s.handle('{"c":"catset","ptt":true}', h)[0])
        s.handle('{"c":"catset","tone":74.4}', h)
        self.assertEqual(json.loads(s.handle('{"c":"cat"}', h)[0])["tn"], 74.4)
        self.assertIn("rejected", s.handle('{"c":"catset","f":-5}', h)[0])
        s.handle('{"c":"cattrack","name":"SO-50","dl":436795000,"ul":145850000,"rr":3.2}', h)
        t = h.cat.tracker
        self.assertEqual((t.name, t.dl, t.ul, t.rr, t.active), ("SO-50", 436795000, 145850000, 3.2, True))
        s.handle('{"c":"cattrack","on":false}', h)
        self.assertFalse(t.enabled)
        s.sub_ms = 1000
        self.assertTrue(any('"e":"cat"' in l for l in s.tick(h)))
        # Configuración
        cfg = h.cfg
        self.assertEqual(cfg.apply_json('{"cat":{"type":"radio"}}'), "cat.type none | hamlib | rigctl | sdrconnect")
        self.assertEqual(cfg.apply_json('{"cat":{"type":"sdrconnect","host":""}}'), "cat.host")
        self.assertEqual(cfg.apply_json('{"cat":{"type":"rigctl","serverPort":4533}}'), "cat.serverPort")
        self.assertEqual(cfg.apply_json('{"cat":{"type":"hamlib","model":3081,"civ":"0xA2"}}'), "")
        argv = C.RigctldProcess(cfg.data["cat"]).argv()
        self.assertEqual(argv[1:], ["-m", "3081", "-T", "127.0.0.1", "-t", "4536", "-r", "/dev/ttyUSB1", "-s", "19200",
                                    "-c", "0xA2"])
        k = C.Cat(cfg.data["cat"])
        self.assertFalse(k.ptt_allowed())
        self.assertIn("Hamlib 3081", k.rig.info())


class TestWeb(unittest.TestCase):
    def test_page(self):
        p = load_page()
        self.assertTrue(p.startswith(b"<!DOCTYPE html>"))
        self.assertNotIn(b"/update", p)


def _stepper_cfg(**k):
    c = json.loads(json.dumps(ST.DEFAULTS))
    for ax in ("az", "el"):
        c[ax].update({"stepsRev": 200, "micro": 1, "gear": 1.8, "dps": 30.0, "accel": 60.0})   # 1 paso por grado
    c.update(k)
    return c


def _run(r, t=0.0, limit=200000):
    """Avanza el reloj de los pasos hasta que todo para; devuelve el tiempo final."""
    for _ in range(limit):
        n = r.step_once(t)
        if n is None:
            return t
        t = max(n, t + 1e-6)
    raise AssertionError("no para")


class TestBoard(unittest.TestCase):
    def test_family(self):
        f = board.family
        self.assertEqual(f("Raspberry Pi 3 Model B Plus Rev 1.3"), "pi3")
        self.assertEqual(f("Raspberry Pi 3 Model B Rev 1.2"), "pi3")
        self.assertEqual(f("Raspberry Pi 5 Model B Rev 1.0"), "pi5")
        self.assertEqual(f("Raspberry Pi Compute Module 5 Rev 1.0"), "pi5")
        self.assertEqual(f("Raspberry Pi 4 Model B Rev 1.5"), "pi4")
        self.assertEqual(f("Raspberry Pi 400 Rev 1.1"), "pi4")
        self.assertEqual(f("Raspberry Pi Zero 2 W Rev 1.0"), "zero2")
        self.assertEqual(f("Linux"), "other")
        i = board.info("Raspberry Pi 5 Model B Rev 1.0")
        self.assertEqual((i["gpio"], i["gps"], i["link"], i["rtc"]), ("pinctrl-rp1", "/dev/ttyAMA0", "uart3-pi5", True))
        i = board.info("Raspberry Pi 3 Model B Plus Rev 1.3")
        self.assertEqual((i["gpio"], i["gps"], i["link"]), ("pinctrl-bcm2835", "/dev/serial0", ""))

    def test_model_file(self):
        p = os.path.join(tempfile.mkdtemp(), "model")
        with open(p, "wb") as f:
            f.write(b"Raspberry Pi 3 Model B Plus Rev 1.3\x00")
        self.assertEqual(board.model(p), "Raspberry Pi 3 Model B Plus Rev 1.3")
        self.assertEqual(board.model(p + "x"), "Linux")

    def test_pick_chip(self):
        # Pi 5 con nucleo antiguo: el conector es gpiochip4; con el nuevo, gpiochip0; los brcmstb no valen
        old = {0: "gpio-brcmstb@107d508500", 1: "gpio-brcmstb@107d508520", 4: "pinctrl-rp1"}
        new = {0: "pinctrl-rp1", 10: "gpio-brcmstb@107d508500", 11: "gpio-brcmstb@107d508520"}
        self.assertEqual(ST.pick_chip(old, "pinctrl-rp1"), 4)
        self.assertEqual(ST.pick_chip(new, "pinctrl-rp1"), 0)
        self.assertEqual(ST.pick_chip({0: "pinctrl-bcm2835", 1: "raspberrypi-exp-gpio"}, "pinctrl-bcm2835"), 0)
        self.assertEqual(ST.pick_chip({0: "x", 2: "pinctrl-bcm2711"}, "pinctrl-rp1"), 2)    # otra etiqueta: primer pinctrl-
        self.assertEqual(ST.pick_chip({0: "x"}, "pinctrl-rp1"), -1)

    def test_step_rate(self):
        s = json.loads(json.dumps(ST.DEFAULTS))
        for ax in ("az", "el"):
            s[ax].update(gear=60.0, dps=4.0)        # 200 x 32 x 60 / 360 x 4 = 4267 pasos/s por motor
        self.assertEqual(ST.validate(s, False, board.FAMILIES["pi5"]["steps"]), "")
        self.assertEqual(ST.validate(s, False, board.FAMILIES["pi4"]["steps"]), "")
        self.assertEqual(ST.validate(s, False, board.FAMILIES["pi3"]["steps"]), "")
        s["el"]["dps"] = 6.0                         # 4267 + 6400 = 10667: no en la 3 ni la Zero 2 W (10000)
        self.assertIn("10000", ST.validate(s, False, board.FAMILIES["pi3"]["steps"]))
        self.assertIn("10000", ST.validate(s, False, board.FAMILIES["zero2"]["steps"]))
        self.assertEqual(ST.validate(s, False, board.FAMILIES["pi4"]["steps"]), "")
        s["el"]["on"] = False                        # un solo motor
        self.assertEqual(ST.validate(s, False, board.FAMILIES["pi3"]["steps"]), "")


class TestStepper(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.state = os.path.join(self.tmp, "st.json")

    def mk(self, **k):
        self.g = ST.FakeGpio()
        return ST.StepperRotor(_stepper_cfg(**k), (90.0, 0.0), gpio=self.g, state_path=self.state)

    def test_defaults_valid_and_pps_clash(self):
        c = config.Config(os.path.join(self.tmp, "c.json"))
        d = json.loads(json.dumps(c.data))
        d["driver"] = "stepper"
        self.assertEqual(config.validate(d), "")
        d["gps"]["on"] = True                              # STEP del motor 2 = GPIO18 = PPS del Uputronics
        self.assertIn("GPIO18", config.validate(d))
        d["stepper"]["el"]["step"] = 26
        self.assertEqual(config.validate(d), "")
        d["stepper"]["el"]["step"] = 13                    # el DIR del motor 1
        self.assertIn("az.dir", config.validate(d))
        self.assertEqual(ST.StepperAxis.steps_per_deg({"stepsRev": 200, "micro": 32, "gear": 60, }), 200 * 32 * 60 / 360)

    def test_goto_steps_and_enable(self):
        r = self.mk()
        self.assertFalse(r.known)
        r.set_pos(45.0, 30.0)
        t = _run(r)
        self.assertAlmostEqual(r.get_pos()[0], 45.0, places=6)
        self.assertAlmostEqual(r.get_pos()[1], 30.0, places=6)
        self.assertEqual(self.g.rises[19], 45)            # STEP del motor 1
        self.assertEqual(self.g.rises[18], 30)            # STEP del motor 2
        self.assertEqual(self.g.level[12], 1)             # ENABLE alto = activo (Rev2.1)
        self.assertGreater(t, 45 / 30.0)                  # la rampa no deja ir mas rapido que dps
        r.step_once(t + 1.0)                              # medio segundo parado: sin corriente
        self.assertEqual(self.g.level[12], 0)
        with open(self.state) as f:
            self.assertAlmostEqual(json.load(f)["az"], 45.0)

    def test_shortest_way_overlap_and_limits(self):
        r = self.mk(maxAz=450.0)
        r.here("az", 350.0)
        r.set_pos(10.0, 0.0)                               # 350 -> 370 (solape) mejor que volver a 10
        _run(r)
        self.assertAlmostEqual(r.axes["az"].deg, 370.0)
        self.assertAlmostEqual(r.get_pos()[0], 10.0)
        r.set_pos(100.0, 95.0)                             # 370 -> 460 no cabe: 100; elevacion al tope 90
        _run(r)
        self.assertAlmostEqual(r.axes["az"].deg, 100.0)
        self.assertAlmostEqual(r.get_pos()[1], 90.0)

    def test_move_stop_ramp_and_direction(self):
        r = self.mk()
        r.here("az", 180.0)
        r.move(P.MOVE_RIGHT, 0)
        t = 0.0
        for _ in range(60):
            t = max(r.step_once(t), t + 1e-6)
        self.assertGreater(r.axes["az"].deg, 180.0)
        self.assertTrue(r.status() & P.ST_MOVING_RIGHT)
        r.stop()
        before = r.axes["az"].deg
        _run(r, t)
        self.assertGreaterEqual(r.axes["az"].deg, before)  # frena, no da la vuelta
        self.assertLess(r.axes["az"].deg - before, 10.0)
        self.assertEqual(r.status(), 0)
        p = r.axes["az"].deg
        r.set_pos(p - 20.0, 0.0)                           # cambio de sentido: DIR baja
        _run(r)
        self.assertAlmostEqual(r.axes["az"].deg, p - 20.0)
        self.assertEqual(self.g.level[13], 0)

    def test_home_switch_and_saved_position(self):
        c = _stepper_cfg()
        c["az"].update({"home": 5, "homeAt": 2.0, "homeDir": -1})
        self.g = ST.FakeGpio()
        r = ST.StepperRotor(c, (0.0, 0.0), gpio=self.g, state_path=self.state)
        self.g.inputs[5] = 1                               # suelto (pullup)
        self.assertEqual(r.home(), "")
        self.assertFalse(r.set_pos(10.0, 0.0))             # buscando: no admite ordenes
        t = 0.0
        for _ in range(30):
            t = max(r.step_once(t), t + 1e-6)
        self.g.inputs[5] = 0                               # pisa el final de carrera
        _run(r, t)
        self.assertTrue(r.known)
        self.assertFalse(r.homing)
        self.assertAlmostEqual(r.axes["az"].deg, 2.0)
        r.set_pos(30.0, 10.0)
        _run(r)
        r2 = ST.StepperRotor(c, (0.0, 0.0), gpio=ST.FakeGpio(), state_path=self.state)
        self.assertTrue(r2.known)
        self.assertAlmostEqual(r2.get_pos()[0], 30.0)
        self.assertAlmostEqual(r2.get_pos()[1], 10.0)
        r3 = ST.StepperRotor(c, (0.0, 0.0), gpio=ST.FakeGpio(), state_path=self.state)
        r3.g.inputs[5] = 1                                 # nunca se pisa: fallo
        r3.home()
        _run(r3)
        self.assertEqual(r3.fault, "HOME AZ")

    def test_api_here_and_home(self):
        class H(api.ApiHost):
            def __init__(s, rot):
                s.rotor = rot
            def here(s, axis, deg):
                return s.rotor.here(axis, deg)
            def home(s):
                return s.rotor.home()
        r = self.mk()
        sess = api.ApiSession()
        out = sess.handle('{"c":"here","axis":"az","deg":123.0}', H(r))
        self.assertIn('"e":"ok"', out[0])
        self.assertAlmostEqual(r.get_pos()[0], 123.0)
        self.assertIn("no home switch", sess.handle('{"c":"home"}', H(r))[0])

    def test_third_axis_inclination(self):
        c = _stepper_cfg()
        c["x"].update({"on": True, "micro": 1, "gear": 1.8, "dps": 30.0, "accel": 60.0})   # 1 paso/grado
        self.g = ST.FakeGpio()
        r = ST.StepperRotor(c, (90.0, 0.0), gpio=self.g, state_path=self.state)
        self.assertTrue(r.has_x)
        self.assertFalse(r.set_axis("az", 10.0))               # az / el solo por set_pos
        self.assertTrue(r.set_axis("x", 30.0))
        _run(r)
        self.assertAlmostEqual(r.get_axis("x"), 30.0, places=6)
        self.assertEqual(self.g.rises[5], 30)                  # STEP del TMC2209 (eje x)
        self.assertEqual(self.g.level[23], 0)                  # ENABLE activo BAJO (enActive 0): 0 = activo
        r.set_axis("x", 90.0)                                  # topes ±41.25
        _run(r)
        self.assertLessEqual(r.get_axis("x"), r.max_x + 1e-6)  # no pasa del tope
        self.assertGreater(r.get_axis("x"), r.max_x - 1.0)     # pero llega cerca (1 paso/grado)
        r.park()                                               # park centra la inclinacion
        _run(r)
        self.assertAlmostEqual(r.get_axis("x"), 0.0, places=3)
        self.assertEqual(r.here("x", 10.0), "")               # Corregir desfase tambien en el eje x
        self.assertAlmostEqual(r.get_axis("x"), 10.0, places=6)

    def test_third_axis_validate(self):
        d = json.loads(json.dumps(ST.DEFAULTS))
        d["x"]["on"] = True
        self.assertEqual(ST.validate(d, False, 20000), "")
        d["x"]["step"] = 13                                    # = DIR del acimut
        self.assertIn("az.dir", ST.validate(d, False, 20000))
        d["x"]["step"] = 5
        d["x"]["enActive"] = 2
        self.assertIn("x.enActive", ST.validate(d, False, 20000))
        d["x"]["enActive"] = 0
        d["minX"], d["maxX"] = 10.0, 10.0
        self.assertIn("minX", ST.validate(d, False, 20000))

    def test_calibration_gain(self):
        r = self.mk()                                          # 1 paso/grado (stepsRev 200, micro 1, gear 1.8)
        self.assertIsNone(r.cal_gain("az", 90.0))              # sin marcar el punto A
        self.assertEqual(r.cal_mark("az"), "")
        r.set_pos(90.0, 0.0)
        _run(r)
        res = r.cal_gain("az", 90.0)                            # giro real = el esperado
        self.assertEqual(res["steps"], 90)
        self.assertAlmostEqual(res["spd"], 1.0)
        self.assertAlmostEqual(res["gear"], 1.8)
        res2 = r.cal_gain("az", 100.0)                          # si de verdad giro 100 grados, menos pasos/grado
        self.assertAlmostEqual(res2["spd"], 0.9)
        self.assertAlmostEqual(res2["gear"], 1.62)
        self.assertEqual(r.cal_mark("zz"), "axis")

    def test_api_calstep(self):
        class H(api.ApiHost):
            rotor = None
            marked = None
            applied = None
            def cal_mark(s, ax):
                s.marked = ax
                return ""
            def cal_gain(s, ax, deg):
                return {"axis": ax, "steps": 90, "spd": 1.0, "gear": 1.8}
            def apply_gain(s, ax, g):
                s.applied = (ax, g)
                return ""
        h, sess = H(), api.ApiSession()
        self.assertIn('"e":"ok"', sess.handle('{"c":"calstep","axis":"az","mark":true}', h)[0])
        self.assertEqual(h.marked, "az")
        self.assertIn('"gear":1.8', sess.handle('{"c":"calstep","axis":"az","deg":90}', h)[0])
        self.assertIsNone(h.applied)
        sess.handle('{"c":"calstep","axis":"az","deg":90,"apply":true}', h)
        self.assertEqual(h.applied, ("az", 1.8))

        class H2(api.ApiHost):
            rotor = None
            def cal_gain(s, ax, deg):
                return None
        self.assertIn("mark first", sess.handle('{"c":"calstep","axis":"az","deg":90}', H2())[0])

    def test_encoder_check(self):
        from aprs_lora_plus_rotor import encoder as E
        c = E.EncoderCheck(cpr=4096, sign=1, offset=0.0, tol=2.0)
        raw90 = round(90 / 360 * 4096)
        self.assertAlmostEqual(c.measured_deg(raw90), 90.0, delta=0.1)
        self.assertFalse(c.lost(raw90, 90.3))
        self.assertTrue(c.lost(raw90, 95.0))
        self.assertAlmostEqual(c.corrected(raw90, 95.0), 90.0, delta=0.2)      # corrige a lo medido
        self.assertAlmostEqual(c.corrected(raw90, 455.0), 450.0, delta=0.3)    # conserva la vuelta (solape az)
        self.assertAlmostEqual(E.wrap180(190.0), -170.0)

    def test_encoder_fault_and_correct(self):
        class FakeEnc:
            def __init__(s):
                s.v = 0
            def raw(s):
                return s.v
        box = {}

        def mk(ec, cache):
            e = FakeEnc()
            box["az"] = e
            return e
        c = _stepper_cfg()
        c["az"]["enc"] = {**ST.DEFAULTS["az"]["enc"], "on": True, "tol": 2.0, "correct": False}
        r = ST.StepperRotor(c, (0.0, 0.0), gpio=ST.FakeGpio(), state_path=self.state, enc_make=mk)
        r.here("az", 100.0)
        box["az"].v = round(100 / 360 * 4096)                 # encoder coincide: sin fallo
        r.check_encoders()
        self.assertEqual(r.fault, "")
        self.assertIn("azEnc", r.raw())
        box["az"].v = round(110 / 360 * 4096)                 # 10 grados de diferencia: pasos perdidos
        r.check_encoders()
        self.assertEqual(r.fault, "ENCODER AZ")

        c2 = _stepper_cfg()
        c2["az"]["enc"] = {**ST.DEFAULTS["az"]["enc"], "on": True, "correct": True}
        box2 = {}

        def mk2(ec, cache):
            e = FakeEnc()
            box2["az"] = e
            return e
        r2 = ST.StepperRotor(c2, (0.0, 0.0), gpio=ST.FakeGpio(), state_path=self.state + "2", enc_make=mk2)
        r2.here("az", 100.0)
        box2["az"].v = round(108 / 360 * 4096)                # con correct: la posicion salta a lo medido
        r2.check_encoders()
        self.assertAlmostEqual(r2.axes["az"].deg, 108.0, delta=0.3)
        self.assertEqual(r2.fault, "")

    def test_stepper_tmc(self):
        class FakeDrv:
            cfg = None
            def configure(s, *a):
                s.cfg = a
            def sg_result(s):
                return 123
        made = {}

        def mk(tc, cache):
            made["x"] = FakeDrv()
            return made["x"]
        c = _stepper_cfg()
        c["x"] = {**ST.DEFAULTS["x"], "on": True, "micro": 1, "gear": 1.8, "dps": 30.0, "accel": 60.0}
        c["x"]["tmc"] = {**ST.DEFAULTS["x"]["tmc"], "on": True, "irun": 20, "sgthrs": 90}
        r = ST.StepperRotor(c, (0.0, 0.0), gpio=ST.FakeGpio(), state_path=self.state, tmc_make=mk)
        self.assertIn("x", r.tmc)
        self.assertEqual(made["x"].cfg[:5], (20, 8, 1, 90, 1000))     # irun, ihold, micro, sgthrs, tcoolthrs
        self.assertEqual(r.sg(), {"x": 123})

    def test_hat_dips_and_info(self):
        c = _stepper_cfg()
        c["az"]["micro"], c["el"]["micro"] = 16, 32
        d = ST.hat_dips(c)
        self.assertFalse(d["software"])
        self.assertEqual(d["az"]["dip"], {"D0": 0, "D1": 0, "D2": 1})   # 1/16 = M2
        self.assertEqual(d["el"]["dip"], {"D3": 1, "D4": 0, "D5": 1})   # 1/32 = M0 + M2
        self.assertEqual(d["az"]["micro"], 16)
        self.assertEqual(ST.hat_dips({**c, "modePins": True}), {"software": True})
        info = ST.hat_info(c)
        self.assertEqual(set(info), {"dips", "supply", "current", "decay", "enable"})


class TestTmc2209(unittest.TestCase):
    def test_crc_and_datagrams(self):
        from aprs_lora_plus_rotor import tmc2209 as T
        self.assertEqual(T.crc8(b""), 0)
        self.assertEqual(T.crc8(bytes([0, 0, 0])), 0)
        self.assertEqual(T.crc8(bytes([1])), 0x89)                   # derivado a mano del polinomio 0x07 (LSB primero)
        d = T.write_datagram(0, T.IHOLD_IRUN, 0x00010203)
        self.assertEqual(len(d), 8)
        self.assertEqual((d[0], d[1], d[2]), (0x05, 0, T.IHOLD_IRUN | 0x80))
        self.assertEqual(d[3:7], bytes([0, 1, 2, 3]))
        self.assertEqual(d[7], T.crc8(d[:7]))
        self.assertEqual(T.read_request(0, T.SG_RESULT)[2], T.SG_RESULT)
        val = 0x12345678
        reply = bytes([0x05, 0xFF, T.SG_RESULT, 0x12, 0x34, 0x56, 0x78])
        reply += bytes([T.crc8(reply)])
        self.assertEqual(T.parse_read_reply(b"\x05\x00\x41\x00" + reply), val)   # con el eco de la peticion delante
        self.assertIsNone(T.parse_read_reply(b"sin respuesta valida"))

    def test_configure_and_sg(self):
        from aprs_lora_plus_rotor import tmc2209 as T

        class FakePort:
            def __init__(s):
                s.writes = []
                s.reply = b""
            def write(s, d):
                s.writes.append(bytes(d))
            def read(s, n):
                return s.reply
            def reset_input_buffer(s):
                pass
        p = FakePort()
        drv = T.Tmc2209(p, 0)
        drv.configure(16, 8, 16, 80, 1000)
        self.assertEqual(len(p.writes), 5)                           # GCONF, IHOLD_IRUN, CHOPCONF, TCOOLTHRS, SGTHRS
        self.assertEqual(p.writes[0][2], T.GCONF | 0x80)
        self.assertEqual(p.writes[4][2], T.SGTHRS | 0x80)
        val = 0x000000AB
        reply = bytes([0x05, 0xFF, T.SG_RESULT, 0, 0, 0, 0xAB])
        p.reply = reply + bytes([T.crc8(reply)])
        self.assertEqual(drv.sg_result(), val)
        self.assertEqual(T.chopconf(16) >> 24 & 0xF, 4)              # 1/16 -> MRES 4


class TestPointing(unittest.TestCase):
    def test_model_fit(self):
        from aprs_lora_plus_rotor import pointing as PT
        true = PT.PointingModel({"ia": 1.5, "ca": 0.8, "np": -0.5, "ie": -0.7, "tf": 0.3})
        sky = [(30, 10), (120, 25), (210, 40), (300, 55), (80, 70), (200, 15)]
        pts = [(sa, se, *true.apply(sa, se)) for sa, se in sky]
        m = PT.PointingModel.fit(pts)
        for sa, se in sky:
            ma, me = m.apply(sa, se)
            ta, te = true.apply(sa, se)
            self.assertAlmostEqual((ma - ta + 180) % 360 - 180, 0.0, places=3)
            self.assertAlmostEqual(me - te, 0.0, places=3)
        self.assertLess(m.residual(pts), 1e-3)
        m1 = PT.PointingModel.fit([(100.0, 30.0, 101.0, 29.0)])       # un punto: solo indices
        self.assertAlmostEqual(m1.p["ia"], 1.0, places=3)
        self.assertAlmostEqual(m1.p["ie"], -1.0, places=3)

    def test_pointing_rotor(self):
        from aprs_lora_plus_rotor import pointing as PT
        f = FakeRotor()
        f.az, f.el = 0.0, 0.0
        pr = PT.PointingRotor(f, PT.PointingModel({"ia": 2.0, "ie": -1.0}), enabled=True)
        pr.set_pos(100.0, 30.0)
        self.assertAlmostEqual(f.az, 102.0)                          # aplica la correccion al mandar
        self.assertAlmostEqual(f.el, 29.0)
        a, e = pr.get_pos()
        self.assertAlmostEqual(a, 100.0)                             # informa el cielo real (inverso)
        self.assertAlmostEqual(e, 30.0)
        pr.enabled = False
        pr.set_pos(100.0, 30.0)
        self.assertAlmostEqual(f.az, 100.0)

    def test_api_pointing(self):
        class H(api.ApiHost):
            rotor = None
            def point_add(s, d):
                return "", {"n": 1, "sky": [180.0, 45.0], "rotor": [181.0, 44.0]}
            def point_fit(s):
                return "", {"params": {"ia": 1.0}, "rms": 0.0, "n": 2}
            def point_clear(s):
                return {"n": 0}
            def point_info(s):
                return {"on": True, "n": 2}
        h, sess = H(), api.ApiSession()
        self.assertIn('"e":"ok"', sess.handle('{"c":"pointadd","target":"sun"}', h)[0])
        self.assertIn('"rms":0', sess.handle('{"c":"pointfit"}', h)[0])
        self.assertIn('"e":"point"', sess.handle('{"c":"pointinfo"}', h)[0])


class TestPeak(unittest.TestCase):
    def test_raster_and_best(self):
        from aprs_lora_plus_rotor import peak as PK
        g = PK.raster(1.0, 0.5)                                   # -1,-0.5,0,0.5,1 en cada eje -> 25 puntos
        self.assertEqual(len(g), 25)
        self.assertIn((0.0, 0.0), g)
        import math
        samples = [(da, de, 100.0 * math.exp(-((da - 0.5) ** 2 + (de + 0.5) ** 2) / 0.05)) for (da, de) in g]
        da, de, pmax = PK.best(samples)                          # pico estrecho (haz de antena): centroide sobre el máximo
        self.assertAlmostEqual(da, 0.5, delta=0.1)
        self.assertAlmostEqual(de, -0.5, delta=0.1)
        self.assertGreater(pmax, 99.0)

    def test_api_peak(self):
        class H(api.ApiHost):
            rotor = None
            started = stopped = False
            def peak_start(s):
                s.started = True
                return ""
            def peak_stop(s):
                s.stopped = True
        h, sess = H(), api.ApiSession()
        self.assertIn('"e":"ok"', sess.handle('{"c":"peak","on":true}', h)[0])
        self.assertTrue(h.started)
        sess.handle('{"c":"peak","on":false}', h)
        self.assertTrue(h.stopped)


class TestAntennas(unittest.TestCase):
    def test_bundled_list_and_get(self):
        names = {a["name"]: a for a in antennas.list_antennas()}
        self.assertIn("fb250plus", names)
        self.assertEqual(names["fb250plus"]["axes"], ["az", "el", "x"])     # FB250+ lleva 3 ejes
        self.assertIn("generic-2axis", names)
        self.assertEqual(names["generic-2axis"]["axes"], ["az", "el"])      # plantilla sin inclinacion
        self.assertIsNone(antennas.get("no-existe"))
        fb = antennas.get("fb250plus")
        self.assertEqual(fb["stepper"]["az"]["gear"], 11.833)              # corona 142T / piñón 12
        self.assertEqual(fb["stepper"]["az"]["micro"], 16)                  # DIP del HAT a 1/16
        self.assertEqual(fb["stepper"]["x"]["enActive"], 0)                 # TMC2209 activo bajo

    def test_apply_profile_over_config(self):
        c = config.Config(os.path.join(tempfile.mkdtemp(), "c.json"))
        fb = antennas.get("fb250plus")
        patch = json.dumps({"driver": "stepper", "stepper": {**fb["stepper"], "antenna": "fb250plus"}})
        self.assertEqual(c.apply_json(patch), "")
        s = c.data["stepper"]
        self.assertTrue(s["x"]["on"])
        self.assertEqual(s["x"]["gear"], 6.0)
        self.assertAlmostEqual(s["minX"], -41.25)
        self.assertEqual(s["antenna"], "fb250plus")
        self.assertEqual(s["x"]["step"], 5)                                # los pines del HAT/TMC se conservan
        self.assertEqual(s["az"]["dir"], 13)

    def test_api_select(self):
        class H(api.ApiHost):
            rotor = None
            picked = None
            def list_antennas(self):
                return [{"name": "fb250plus", "title": "Satlink FB250+"}]
            def select_antenna(self, name):
                self.picked = name
                return "" if name == "fb250plus" else "unknown antenna"
        h, sess = H(), api.ApiSession()
        self.assertIn("fb250plus", sess.handle('{"c":"antennas"}', h)[0])
        self.assertIn('"e":"ok"', sess.handle('{"c":"antenna","name":"fb250plus"}', h)[0])
        self.assertEqual(h.picked, "fb250plus")
        self.assertIn("unknown antenna", sess.handle('{"c":"antenna","name":"x"}', h)[0])


class TestGeo(unittest.TestCase):
    def test_bearing_distance(self):
        b, d = geo.great_circle(0, 0, 0, 1)            # 1 grado al este en el ecuador
        self.assertAlmostEqual(b, 90.0, places=4)
        self.assertAlmostEqual(d / 1000, 111.19, places=0)

    def test_elevation(self):
        _, el, _ = geo.azel(0, 0, 0, 0, 0.0899, 0, True)   # 10 km a nivel: curvatura -> ligeramente negativa
        self.assertTrue(-0.5 < el < 0.0)
        _, el2, _ = geo.azel(0, 0, 0, 0, 0.018, 100, True)  # +100 m a ~2 km
        self.assertAlmostEqual(el2, 2.85, places=1)

    def test_locator(self):
        self.assertEqual(geo.latlon_to_locator(40.4168, -3.7038, 6), "IN80DK")
        ll = geo.locator_to_latlon("IN80DK")
        self.assertAlmostEqual(ll[0], 40.44, places=1)
        self.assertAlmostEqual(ll[1], -3.71, places=1)
        self.assertIsNone(geo.locator_to_latlon("ABCD"))   # formato invalido


class TestP2P(unittest.TestCase):
    class Fake:
        has_el = True
        min_el, max_el, min_az, max_az = 0.0, 90.0, 0.0, 360.0
        link_ok = True
        def __init__(self):
            self.calls = []
        def set_pos(self, az, el):
            self.calls.append((round(az, 1), round(el, 1))); return True
        def set_az(self, az):
            self.calls.append((round(az, 1), None)); return True
        def stop(self): return True
        def park(self): return True
        def stop_az(self): return True
        def move(self, *a, **k): return True

    def test_points_at_peer(self):
        f = self.Fake()
        sr = sky.SkyRotor(f, step=0.5, period=0.0)
        sr.set_peer(0.0, 1.0, 0.0, "fixed")                # otra estacion al este
        self.assertEqual(sr.start(sky.P2P, (0.0, 0.0), now=1.0), "")
        self.assertAlmostEqual(f.calls[-1][0], 90.0, places=0)
        ev = sr.info_event()
        self.assertEqual(ev["t"], "p2p")
        self.assertIn("km", ev)

    def test_needs_peer(self):
        sr = sky.SkyRotor(self.Fake(), period=0.0)
        self.assertIn("peer", sr.start(sky.P2P, (0.0, 0.0), now=1.0))

    def test_peer_update_reaims(self):
        f = self.Fake()
        sr = sky.SkyRotor(f, step=0.5, period=0.0)
        sr.set_peer(0.0, 1.0, 0.0)
        sr.start(sky.P2P, (0.0, 0.0), now=1.0)
        n0 = len(f.calls)
        sr.set_peer(1.0, 0.0, 0.0)                          # ahora al norte
        sr.tick(now=1.0)
        self.assertGreater(len(f.calls), n0)
        self.assertAlmostEqual(f.calls[-1][0], 0.0, places=0)

    def test_aprs_beacon_parse(self):
        fr = "EA7JRS-12>APZLRR:!3736.45N/00059.07Wr estado"
        p = aprs.parse_position(fr, "EA7JRS")
        self.assertIsNotNone(p)
        self.assertAlmostEqual(p[0], 37.6075, places=2)
        self.assertAlmostEqual(p[1], -0.9845, places=2)
        self.assertIsNone(aprs.parse_position(fr, "EB1XYZ"))


class TestGnss(unittest.TestCase):
    def test_gpsd_json(self):
        g = gnss.OwnGps({"on": True, "host": "127.0.0.1", "port": 2947})
        self.assertIsNone(g.position(0.0))
        g.feed('{"class":"TPV","device":"/dev/serial0","mode":3,"lat":36.7213,"lon":-4.4214,"altMSL":12.5}', 1.0)
        g.feed('{"class":"SKY","satellites":[{"PRN":1,"used":true},{"PRN":2,"used":false},{"PRN":3,"used":true}]}', 1.0)
        g.feed('{"class":"PPS","device":"/dev/pps0"}', 1.5)
        self.assertEqual(g.position(2.0), (36.7213, -4.4214))
        i = g.info(2.0)
        self.assertEqual((i["fix"], i["sats"], i["seen"], i["pps"], i["alt"]), (3, 2, 3, True, 12.5))
        self.assertFalse(g.info(9.0)["pps"])
        self.assertIsNone(g.position(20.0))                # posicion vieja
        g.feed('{"class":"TPV","mode":1}', 21.0)
        self.assertIsNone(g.position(21.0))
        g.feed("no json", 22.0)

    def test_hold(self):
        g = gnss.OwnGps(dict(config.DEFAULTS["gps"], on=True))   # hold, holdStart 0.97, holdStop 0.5 m/s, holdM 15 m
        g.feed('{"class":"TPV","mode":3,"lat":36.721300,"lon":-4.421400,"speed":0.0}', 1.0)
        self.assertTrue(g.held)
        self.assertEqual(g.position(1.0), (36.721300, -4.421400))
        # baile del fix (~2 m) con velocidad baja: se devuelve la referencia congelada, no la fluctuacion
        g.feed('{"class":"TPV","mode":3,"lat":36.721315,"lon":-4.421390,"speed":0.2}', 2.0)
        self.assertEqual(g.position(2.0), (36.721300, -4.421400))
        self.assertTrue(g.held)
        # histeresis: hacen falta 3 lecturas rapidas seguidas para darlo por en marcha
        g.feed('{"class":"TPV","mode":3,"lat":36.721320,"lon":-4.421390,"speed":2.0}', 3.0)
        self.assertTrue(g.held)                                 # 1 lectura: aun parado
        g.feed('{"class":"TPV","mode":3,"lat":36.721340,"lon":-4.421390,"speed":2.0}', 4.0)
        self.assertTrue(g.held)                                 # 2 lecturas
        g.feed('{"class":"TPV","mode":3,"lat":36.721360,"lon":-4.421390,"speed":2.0}', 5.0)
        self.assertFalse(g.held)                                # 3 lecturas: en marcha, sigue el fix en vivo
        self.assertEqual(g.position(5.0), (36.721360, -4.421390))
        # salto grande (~30 m) sin velocidad fiable: reubica la referencia
        g.feed('{"class":"TPV","mode":2,"lat":36.721300,"lon":-4.421400,"speed":0.0}', 6.0)
        g.feed('{"class":"TPV","mode":2,"lat":36.721300,"lon":-4.421400,"speed":0.0}', 7.0)
        g.feed('{"class":"TPV","mode":2,"lat":36.721300,"lon":-4.421400,"speed":0.0}', 8.0)
        self.assertTrue(g.held)                                 # vuelto a parado
        g.feed('{"class":"TPV","mode":2,"lat":36.721570,"lon":-4.421400}', 9.0)   # ~30 m, sin speed
        self.assertFalse(g.held)
        self.assertEqual(g.position(9.0), (36.721570, -4.421400))
        # hold desactivado: siempre el fix en bruto
        g2 = gnss.OwnGps(dict(config.DEFAULTS["gps"], on=True, hold=False))
        g2.feed('{"class":"TPV","mode":3,"lat":36.721300,"lon":-4.421400}', 1.0)
        g2.feed('{"class":"TPV","mode":3,"lat":36.721305,"lon":-4.421404}', 2.0)
        self.assertEqual(g2.position(2.0), (36.721305, -4.421404))
        self.assertFalse(g2.held)

    def test_ubx_valset(self):
        # UBX-CFG-VALSET del static hold del M10, igual que ubxValset del tracker
        f = gnss._ubx_valset([(0x20250038, 20), (0x3025003B, 2)])
        self.assertEqual(f[:4], b"\xB5\x62\x06\x8A")            # cabecera + clase/id
        payload = bytes([0, 3, 0, 0]) + bytes([0x38, 0x00, 0x25, 0x20, 20]) + bytes([0x3B, 0x00, 0x25, 0x30, 2, 0])
        self.assertEqual(f[4:6], len(payload).to_bytes(2, "little"))
        self.assertEqual(f[6:6 + len(payload)], payload)       # SPEED_THRS 1 byte, DIST_THRS 2 bytes
        ca = cb = 0                                             # suma de Fletcher comprobada aparte
        for b in f[2:-2]:
            ca = (ca + b) & 0xFF
            cb = (cb + ca) & 0xFF
        self.assertEqual(f[-2:], bytes([ca, cb]))

    def test_ubx_cfg_nav5_hold(self):
        # UBX-CFG-NAV5 del static hold para u-blox 8 (el del Uputronics GPS HAT)
        f = gnss._ubx_cfg_nav5_hold(20, 2)
        self.assertEqual(f[:4], b"\xB5\x62\x06\x24")            # cabecera + clase/id
        self.assertEqual(f[4:6], (36).to_bytes(2, "little"))   # payload fijo de 36 bytes
        p = f[6:6 + 36]
        self.assertEqual(p[0:2], (0x0040).to_bytes(2, "little"))   # mask: solo static hold (bit 6)
        self.assertEqual(p[22], 20)                            # staticHoldThresh (cm/s)
        self.assertEqual(p[28:30], (2).to_bytes(2, "little"))  # staticHoldMaxDist (m)
        self.assertEqual(sum(p[2:22]) + sum(p[23:28]) + sum(p[30:]), 0)   # el resto a 0: no toca nada mas
        ca = cb = 0
        for b in f[2:-2]:
            ca = (ca + b) & 0xFF
            cb = (cb + ca) & 0xFF
        self.assertEqual(f[-2:], bytes([ca, cb]))

    def test_ublox_config_select(self):
        # m8 -> CFG-NAV5 (0x06 0x24); m10 y x20 -> CFG-VALSET (0x06 0x8A); valores validos en config
        for chip in ("", "m8", "m10", "x20"):
            self.assertEqual(config.validate(dict(config.DEFAULTS, gps=dict(config.DEFAULTS["gps"], ublox=chip))), "")
        self.assertIn("ublox", config.validate(dict(config.DEFAULTS, gps=dict(config.DEFAULTS["gps"], ublox="f9"))))
        valset = gnss._ubx_valset([(0x20250038, 20), (0x3025003B, 2)])     # el que usan m10 y x20
        self.assertEqual(valset[2:4], b"\x06\x8A")
        self.assertEqual(gnss._ubx_cfg_nav5_hold(20, 2)[2:4], b"\x06\x24")  # el que usa m8

    def test_own_gps_first(self):
        g = gnss.OwnGps({"on": True, "host": "127.0.0.1", "port": 2947})
        ln = L.Lan(dict(config.DEFAULTS["lan"]), {"lat": 40.0, "lon": -3.0}, WindGuard(config.DEFAULTS["wind"]), None)
        self.assertEqual(ln.position(), (40.0, -3.0))
        ln.own_pos = g.position
        g.feed('{"class":"TPV","mode":2,"lat":36.5,"lon":-6.2}')
        self.assertEqual(ln.position(), (36.5, -6.2))


ISS_L1 = "1 25544U 98067A   24331.54791435  .00016717  00000-0  30177-3 0  9004"
ISS_L2 = "2 25544  51.6400 210.3900 0004700  90.0000 270.0000 15.49500000 10000"
SO50_L1 = "1 27607U 02058C   24331.50000000  .00000100  00000-0  10000-3 0  9990"
SO50_L2 = "2 27607  64.5500 100.0000 0070000 200.0000 160.0000 14.75000000 10000"
TLE_TXT = "ISS (ZARYA)\n%s\n%s\nSO-50\n%s\n%s\n" % (ISS_L1, ISS_L2, SO50_L1, SO50_L2)


class TestSat(unittest.TestCase):
    def test_sgp4_vendored(self):
        from aprs_lora_plus_rotor.sgp4 import Satrec, jday
        s = Satrec.twoline2rv(ISS_L1, ISS_L2)
        jd, fr = jday(2026, 11, 28, 9, 47, 0.0)
        err, r, _ = s.sgp4(jd, fr)
        self.assertEqual(err, 0)
        for got, exp in zip(r, (-1513.627, -4688.466, 4472.890)):    # verificado contra la libreria sgp4 de referencia
            self.assertAlmostEqual(got, exp, delta=1e-2)

    def test_enu_look(self):
        from aprs_lora_plus_rotor import sat
        obs = sat._observer_ecef(0.0, 0.0, 0.0)
        self.assertAlmostEqual(sat._look((7000, 0, 0), 0, 0, obs)[1], 90.0, delta=0.1)   # arriba -> el 90
        self.assertAlmostEqual(sat._look((6378.137, 1000, 0), 0, 0, obs)[0], 90.0, delta=0.5)  # este -> az 90
        self.assertAlmostEqual(sat._look((6378.137, 0, 1000), 0, 0, obs)[0], 0.0, delta=0.5)   # norte -> az 0

    def test_parse_tle(self):
        from aprs_lora_plus_rotor import sat
        sats = sat.parse_tle(TLE_TXT)
        self.assertEqual([s.name for s in sats], ["ISS (ZARYA)", "SO-50"])
        self.assertEqual([s.catnum for s in sats], ["25544", "27607"])
        s = sats[0]
        o = s.observe(37.6, -0.98, 45, 1795000000.0)
        self.assertEqual(len(o), 3)
        self.assertTrue(-90.0 <= o[1] <= 90.0 and o[2] > 0)         # elevacion y distancia con sentido

    def test_project_format(self):
        from aprs_lora_plus_rotor import sat
        txt = ("# cabecera\n"
               "S|25544|ISS (ZARYA)|U|sa\n" + ISS_L1 + "\n" + ISS_L2 + "\n"
               "T|0|0|145800000|145800000|FM|0|0|voice\n"
               "S|53109|GREENCUBE (IO-117)|G|cg\n" + SO50_L1.replace("27607", "53109") + "\n"
               + SO50_L2.replace("27607", "53109") + "\n")
        ss = sat.parse_sats(txt)
        self.assertEqual([s.name for s in ss], ["ISS (ZARYA)", "GREENCUBE (IO-117)"])
        self.assertEqual(ss[0].groups, "saE")                # 'E' de LEO, derivada del movimiento medio del TLE
        self.assertEqual(ss[1].groups, "cgE")                # 'g' de la categoria GEO, sin duplicar; 'E' = LEO

    def test_derived_groups(self):
        from aprs_lora_plus_rotor import sat
        inm = ("S|23839|INMARSAT 3-F1|G|g\n"
               "1 23839U 96020A   26331.13174167 -.00000256  00000+0  00000+0 0  9999\n"
               "2 23839  10.9367  45.0977 0008556 206.4043 322.1371  0.99980242111889\n")
        s = sat.parse_sats(inm)[0]
        self.assertIn("I", s.groups)                         # Inmarsat por el nombre
        self.assertNotIn("E", s.groups)                      # geoestacionario, no LEO
        db = sat.SatDb({"tleUrl": "x", "amsat": False})
        db._set(sat.parse_sats(inm) + sat.parse_sats(TLE_TXT), 1e9)
        names = [g[1] for g in db.groups()]
        self.assertEqual(names, sorted(names, key=str.lower))   # orden alfabetico
        self.assertIn("Inmarsat", names)

    def test_amsat_active(self):
        from aprs_lora_plus_rotor import sat
        j = ('{"data":[{"name":"IO-117","report":"Heard"},{"name":"QO-100","report":"Telemetry Only"},'
             '{"name":"AO-91","report":"Not Heard"}],"meta":{"hours":24}}')
        keys = sat.amsat_active(j)
        self.assertEqual(keys, {"IO117", "QO100"})           # 'Not Heard' fuera
        self.assertEqual(sat.sat_name_keys("GREENCUBE (IO-117)"), {"GREENCUBE", "IO117"})

    def test_passes_filters(self):
        from aprs_lora_plus_rotor import sat
        db = self._db()
        db.active_keys = {"ISS"}                              # ISS activo segun AMSAT
        rows = db.passes(37.6, -0.98, 45, now=1795000000.0)
        self.assertEqual(len(rows), 2)
        self.assertTrue(all("act" in r and "grp" in r for r in rows))
        iss = next(r for r in rows if r["name"].startswith("ISS"))
        self.assertTrue(iss["act"])
        self.assertEqual(len(db.passes(37.6, -0.98, 45, now=1795000000.0, active=True)), 1)   # solo ISS
        self.assertEqual(len(db.passes(37.6, -0.98, 45, now=1795000000.0, group="s")), 1)     # solo estaciones

    def _db(self):
        from aprs_lora_plus_rotor import sat
        txt = ("S|25544|ISS (ZARYA)|U|s\n" + ISS_L1 + "\n" + ISS_L2 + "\n"
               "S|27607|SO-50|U|a\n" + SO50_L1 + "\n" + SO50_L2 + "\n")
        db = sat.SatDb({"tleUrl": "https://x/y", "horizonH": 8, "amsat": True, "amsatUrl": "https://x/a"})
        db._set(sat.parse_sats(txt), 1795000000.0)
        return db

    def test_sky_sat(self):
        db = self._db()
        f = FakeRotor()
        s = sky.SkyRotor(f, period=0.0)
        self.assertEqual(s.start(sky.SAT, (37.6, -0.98), 1795000000.0, 45), "no satellite selected")
        s.set_sat(db.get("ISS (ZARYA)"))
        self.assertEqual(s.start(sky.SAT, (37.6, -0.98), 1795000000.0, 45), "")
        ev = s.info_event()
        self.assertEqual(ev["t"], "sat")
        self.assertEqual(ev["name"], "ISS (ZARYA)")
        self.assertIn("km", ev)
        self.assertTrue(s.set_pos(10, 10) is not False and not s.tracking)   # una orden deja de seguir
        self.assertEqual(sky.parse("sat"), sky.SAT)

    def test_api_sat(self):
        db = self._db()
        sr = sky.SkyRotor(FakeRotor(), period=0.0)

        class H(api.ApiHost):
            def __init__(h):
                h.satdb, h.sky, h.rotor, h.wind, h.cat = db, sr, sr, None, None
            def pin(h):
                return ""
            def list_sats(h, group=None, active=False):
                return h.satdb.passes(37.6, -0.98, 45, group=group, active=active)
            def sat_info(h):
                return {"updated": int(h.satdb.updated), "n": len(h.satdb.sats),
                        "name": h.sky.sat.name if h.sky.sat else "", "haspos": True}
            def select_sat(h, key):
                s = h.satdb.get(key)
                if s is None:
                    return "unknown satellite"
                h.sky.set_sat(s)
                return ""
            def update_tle(h):
                return ""
            def sky_start(h, tg):
                return h.sky.start(tg, (37.6, -0.98), 1795000000.0, 45)

        h = H()
        ses = api.ApiSession()
        out = json.loads(ses.handle('{"c":"sats"}', h)[0])
        self.assertEqual(out["e"], "sats")
        self.assertEqual(out["n"], 2)
        self.assertTrue(isinstance(out["list"], list))
        self.assertIn('"err"', ses.handle('{"c":"track","t":"sat","id":"NOPE"}', h)[0])
        self.assertEqual(ses.handle('{"c":"track","t":"sat","id":"SO-50"}', h)[0], '{"e":"ok","c":"track"}')
        self.assertIn('"t":"sat"', ses.handle('{"c":"get"}', h)[0])
        self.assertEqual(ses.handle('{"c":"tle"}', h)[0], '{"e":"ok","c":"tle"}')

    def test_config_sat(self):
        c = json.loads(json.dumps(config.DEFAULTS))
        self.assertEqual(config.validate(c), "")
        self.assertIn("sat.tleUrl", config.validate({**c, "sat": dict(c["sat"], tleUrl="ftp://x")}))
        self.assertIn("sat.horizonH", config.validate({**c, "sat": dict(c["sat"], horizonH=0.1)}))


class TestAttitude(unittest.TestCase):
    def test_math(self):
        from aprs_lora_plus_rotor import attitude as A
        for az, el in [(0, 0), (90, 10), (217, 45), (350, 80)]:
            am, em = A.aim(az, el, 0, 0, 0)
            self.assertAlmostEqual(am, az, 6)
            self.assertAlmostEqual(em, el, 6)
        # escora a estribor sube la elevacion de un objetivo en el horizonte a estribor
        am, em = A.aim(90, 0, 20, 0, 0)
        self.assertAlmostEqual(am, 90, 3)
        self.assertAlmostEqual(em, 20, 3)
        # ida y vuelta
        am, em = A.aim(123.4, 33.3, -12, 8, 5)
        a2, e2 = A.unaim(am, em, -12, 8, 5)
        self.assertAlmostEqual(a2, 123.4, 4)
        self.assertAlmostEqual(e2, 33.3, 4)

    def test_rotor(self):
        from aprs_lora_plus_rotor.attitude import AttitudeRotor
        f = FakeRotor()
        a = AttitudeRotor(f, enabled=False)
        a.set_pos(100, 30)
        self.assertEqual((f.az, f.el), (100, 30))                 # apagado: sin cambios
        a.enabled = True
        a.set_attitude(20, 0, 0)
        a.set_pos(90, 0)
        self.assertAlmostEqual(f.az, 90, 3)
        self.assertAlmostEqual(f.el, 20, 3)                       # escora 20 -> el 20
        az, el = a.get_pos()
        self.assertAlmostEqual(az, 90, 3)
        self.assertAlmostEqual(el, 0, 3)                          # la posicion informada vuelve a nivelada
        a2 = AttitudeRotor(FakeRotor(), enabled=True, yaw_sign=-1)
        a2.set_attitude(0, 0, 10)
        self.assertAlmostEqual(a2.yaw, -10, 6)                    # el signo calibra el eje
        ev = a.info_event()
        self.assertEqual(ev["on"], True)
        self.assertEqual(ev["r"], 20.0)

    def test_tare(self):
        from aprs_lora_plus_rotor.attitude import AttitudeRotor
        a = AttitudeRotor(FakeRotor(), enabled=True)
        a.set_attitude(20, 5, 0)
        self.assertEqual(a.tare(), (20.0, 5.0, 0.0))     # el cero se toma de la actitud actual
        self.assertAlmostEqual(a.roll, 0, 6)
        self.assertAlmostEqual(a.pitch, 0, 6)            # parte de escora y cabeceo 0
        a.set_attitude(22, 5, 0)
        self.assertAlmostEqual(a.roll, 2, 6)             # a partir de ahi, relativa al cero
        self.assertAlmostEqual(a.pitch, 0, 6)
        a.set_zero(0, 0, 0)
        self.assertAlmostEqual(a.roll, 22, 6)            # cero manual a 0: vuelve a valores absolutos

    def test_api_and_config(self):
        f = FakeRotor()

        class H(api.ApiHost):
            def __init__(h):
                from aprs_lora_plus_rotor.attitude import AttitudeRotor
                h.att = AttitudeRotor(f, enabled=True)
                h.rotor = h.att
                h.wind = h.cat = None
            def pin(h):
                return ""
            def set_attitude(h, r, p, y=0.0):
                h.att.set_attitude(r, p, y)
                return ""

        ses = api.ApiSession()
        self.assertEqual(ses.handle('{"c":"att","r":20,"p":0,"y":0}', H())[0], '{"e":"ok","c":"att"}')
        c = json.loads(json.dumps(config.DEFAULTS))
        self.assertEqual(config.validate(c), "")
        self.assertIn("att.step", config.validate({**c, "att": dict(c["att"], step=0)}))
        self.assertIn("rollSign", config.validate({**c, "att": dict(c["att"], rollSign=2)}))


if __name__ == "__main__":
    unittest.main()
