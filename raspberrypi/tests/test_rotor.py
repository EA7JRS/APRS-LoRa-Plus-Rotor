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

from aprs_lora_plus_rotor import api, aprs, config, kiss, protocols as P  # noqa: E402
from aprs_lora_plus_rotor import cat as C  # noqa: E402
from aprs_lora_plus_rotor.hamlib import HamlibRotor  # noqa: E402
from aprs_lora_plus_rotor.web import load_page  # noqa: E402
from aprs_lora_plus_rotor.wind import WindGuard, WindRotor  # noqa: E402
from aprs_lora_plus_rotor import lan as L  # noqa: E402
from aprs_lora_plus_rotor.heading import HeadingRotor, ang_diff  # noqa: E402
from aprs_lora_plus_rotor import astro, sky, wmm  # noqa: E402


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
        az, el, dist = astro.moon_position(t, 36.53, -6.29)
        self.assertAlmostEqual(el, 22.708, delta=0.03); self.assertAlmostEqual(az, 162.424, delta=0.03)
        self.assertTrue(400000 < dist < 410000)
        self.assertAlmostEqual(astro.refraction(0), 0.48, delta=0.03)
        self.assertEqual([astro.beaufort(k) for k in (0, 0.5, 3.4, 10, 38.9, 70)], [0, 1, 1, 3, 8, 12])

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


if __name__ == "__main__":
    unittest.main()
