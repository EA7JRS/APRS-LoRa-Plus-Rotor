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
from aprs_lora_plus_rotor.hamlib import HamlibRotor  # noqa: E402
from aprs_lora_plus_rotor.web import load_page  # noqa: E402


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


class TestWeb(unittest.TestCase):
    def test_page(self):
        p = load_page()
        self.assertTrue(p.startswith(b"<!DOCTYPE html>"))
        self.assertNotIn(b"/update", p)


if __name__ == "__main__":
    unittest.main()
