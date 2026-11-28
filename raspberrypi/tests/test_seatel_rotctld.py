# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pruebas de seatel_rotctld con la PCU simulada (ninguna E/S a un puerto real): python3 -m unittest -v"""

import os
import socket
import sys
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import seatel_rotctld as m  # noqa: E402


def wait(cond, timeout=6.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.02)
    return False


def ae(sim):
    return [w for w in sim.writes if w[:1] in (b"A", b"E")]


class Base(unittest.TestCase):
    kw = {}

    def setUp(self):
        self.sim = m.SimPcu(180.0, 45.0, speed=20.0)
        self.rot = m.SeaTel("sim", transport=self.sim, poll=0.2, min_interval=0.5, **{"allow_motion": True, **self.kw})
        self.rot.start()
        self.assertTrue(wait(lambda: self.rot.get_pos() is not None))

    def tearDown(self):
        self.rot.shutdown()
        self.rot.join(2)

    def at(self, az, el):
        p = self.rot.get_pos()
        return p is not None and abs(m.angle_diff(p[0], az)) <= 0.2 and abs(p[1] - el) <= 0.2


class Frames(unittest.TestCase):
    def test_packets(self):
        self.assertEqual(m.packet("A", 2203), bytes.fromhex("41 32 32 30 33 88 0D"))
        self.assertEqual(m.packet("E", 451), bytes.fromhex("45 30 34 35 31 8F 0D"))

    def test_parse(self):
        body = b"E0451A2193C0900"
        frame = body + bytes((m.checksum(body),))
        self.assertEqual(m.parse_p(frame)[:2], (219.3, 45.1))
        self.assertEqual(m.parse_p(b"**" + frame)[3], b"**")
        self.assertIsNone(m.parse_p(body + b"\x80"))
        self.assertIsNone(m.parse_p(frame[1:]))


class Motion(Base):
    def test_goto_both_axes_one_pair(self):
        self.assertEqual(self.rot.set_pos(200.0, 30.0), m.E_OK)
        self.assertTrue(wait(lambda: self.at(200.0, 30.0) and not self.rot.moving()))
        self.assertEqual(ae(self.sim), [m.packet("A", 2000), m.packet("E", 300)])
        self.assertEqual(self.rot.fault, "")

    def test_a_e_gap_and_no_resend(self):
        self.rot.set_pos(181.0, 45.0)
        self.assertTrue(wait(lambda: len(ae(self.sim)) == 2))
        time.sleep(1.2)
        self.assertEqual(len(ae(self.sim)), 2)               # la misma consigna no se repite

    def test_setpoint_replaced_while_moving(self):
        self.sim.speed = 2.0
        self.rot.set_pos(200.0, 45.0)
        self.assertTrue(wait(lambda: len(ae(self.sim)) == 2))
        self.rot.set_pos(185.0, 50.0)
        self.assertTrue(wait(lambda: self.at(185.0, 50.0), 12))
        self.assertEqual(len(ae(self.sim)), 4)

    def test_elevation_clamped_and_az_wrapped(self):
        self.rot.set_pos(-170.0, 2.0)                        # -170 = 190; EL 2 → límite 5
        self.assertTrue(wait(lambda: self.at(190.0, 5.0)))

    def test_north_crossing_rejected(self):
        self.sim.pos[:] = [350.0, 45.0]
        self.sim.tgt[:] = [350.0, 45.0]
        self.assertTrue(wait(lambda: self.at(350.0, 45.0)))
        self.assertEqual(self.rot.set_pos(10.0, 45.0), m.E_INVAL)
        time.sleep(1.0)
        self.assertEqual(ae(self.sim), [])

    def test_stop_cancels_pending(self):
        self.rot.min_interval = 30.0
        self.rot._t_sent = time.monotonic()
        self.rot.set_pos(200.0, 45.0)
        self.rot.stop()
        self.rot.min_interval = 0.5
        time.sleep(1.2)
        self.assertEqual(ae(self.sim), [])

    def test_fault_on_telemetry_loss_and_clear(self):
        m.TELEMETRY_LOST, old = 1.0, m.TELEMETRY_LOST
        try:
            self.sim.speed = 0.5
            self.rot.set_pos(220.0, 45.0)
            self.assertTrue(wait(lambda: len(ae(self.sim)) == 2))
            self.sim.mute = True
            self.assertTrue(wait(lambda: bool(self.rot.fault)))
            self.sim.mute = False
            self.assertTrue(wait(lambda: self.rot.get_pos() is not None))
            self.assertEqual(self.rot.set_pos(190.0, 45.0), m.E_REJECTED)
            self.rot.clear()
            self.assertEqual(self.rot.set_pos(190.0, 45.0), m.E_OK)
        finally:
            m.TELEMETRY_LOST = old

    def test_partial_tx_latches_fault_without_retry(self):
        self.sim.fail_on = b"E"
        self.rot.set_pos(190.0, 40.0)
        self.assertTrue(wait(lambda: bool(self.rot.fault)))
        time.sleep(0.5)
        self.assertEqual(sum(w[:1] == b"A" for w in self.sim.writes), 1)


class NorthCrossing(Base):
    kw = {"north_crossing": True}

    def test_crossing_allowed(self):
        self.sim.pos[:] = [350.0, 45.0]
        self.sim.tgt[:] = [350.0, 45.0]
        self.assertTrue(wait(lambda: self.at(350.0, 45.0)))
        self.rot.set_pos(10.0, 45.0)
        self.assertTrue(wait(lambda: self.at(10.0, 45.0) and not self.rot.moving()))
        self.assertEqual(self.rot.fault, "")


class Hold(Base):
    kw = {"stop_mode": "hold"}

    def test_hold_sends_current_position(self):
        self.sim.speed = 1.0
        self.rot.set_pos(220.0, 45.0)
        self.assertTrue(wait(lambda: len(ae(self.sim)) == 2))
        time.sleep(1.0)
        self.rot.stop()
        self.assertTrue(wait(lambda: len(ae(self.sim)) == 4))
        self.assertLess(int(ae(self.sim)[2][1:5]), 1900)


class DryRun(Base):
    kw = {"allow_motion": False}

    def test_no_ae_without_flag(self):
        self.assertEqual(self.rot.set_pos(200.0, 30.0), m.E_OK)
        time.sleep(1.5)
        self.assertTrue(self.sim.writes and all(w == b"P" for w in self.sim.writes))


class Offset(Base):
    kw = {"az_offset": 10.0}

    def test_offset(self):
        self.assertAlmostEqual(self.rot.get_pos()[0], 190.0, places=1)
        self.rot.set_pos(200.0, 45.0)
        self.assertTrue(wait(lambda: len(ae(self.sim)) == 2))
        self.assertEqual(ae(self.sim)[0], m.packet("A", 1900))


class Network(Base):
    def ask(self, sock, text):
        sock.sendall(text.encode())
        time.sleep(0.15)
        return sock.recv(4096).decode()

    def test_rotctl_tcp_and_udp(self):
        servers = m.serve_rotctl(self.rot, "127.0.0.1", 0, 0)
        port = servers[0].server_address[1]
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=2) as s:
                az, el = self.ask(s, "p\n").split()
                self.assertAlmostEqual(float(az), 180.0, places=1)
                self.assertAlmostEqual(float(el), 45.0, places=1)
                self.assertEqual(self.ask(s, "P 190.0 50.0\n"), "RPRT 0\n")
                self.assertTrue(wait(lambda: self.at(190.0, 50.0)))
                state = self.ask(s, "\\dump_state\n")
                self.assertTrue(state.startswith("1\n2\nmin_az=0.000000\nmax_az=360.000000\n") and state.endswith("done\n"))
                self.assertEqual(self.ask(s, "M 8 50\n"), "RPRT -11\n")
                self.assertEqual(self.ask(s, "s\n"), "0\n")
                self.assertEqual(self.ask(s, "+\\get_info\n").split("\n")[0], "get_info:")
                self.assertEqual(self.ask(s, "S\n"), "RPRT 0\n")
                self.assertEqual(self.ask(s, "P 190 x\n"), "RPRT -1\n")
                self.assertIn("SeaTel rotctld", self.ask(s, "_\n"))
                self.assertEqual(self.ask(s, "q\n"), "RPRT 0\n")
        finally:
            servers[0].shutdown()
            servers[0].server_close()

    def test_lx200_bridge(self):
        lat, lon = 38.5878, -0.6714
        bridge = m.Lx200Bridge(self.rot, lat, lon)
        ra, dec = m.azel_to_radec(185.0, 50.0, lat, lon, time.time())
        st = {}
        h, d = int(ra), abs(dec)
        self.assertEqual(bridge.command(f":Sr{h:02d}:{int(ra % 1 * 60):02d}:{int(ra * 3600 % 60):02d}", st), "1")
        self.assertEqual(bridge.command(f":Sd{'-' if dec < 0 else '+'}{int(d):02d}*{int(d % 1 * 60):02d}:{int(d * 3600 % 60):02d}", st), "1")
        self.assertEqual(bridge.command(":MS", st), "0")
        self.assertTrue(wait(lambda: self.at(185.0, 50.0)))
        self.assertRegex(bridge.command(":GR", st), r"^\d\d:\d\d:\d\d#$")
        bridge.command(":Q", st)
        self.assertIsNone(bridge.target)


class Astro(unittest.TestCase):
    def test_roundtrip(self):
        t = 1795474800.0
        for az, el in ((10.0, 20.0), (185.0, 50.0), (300.0, 75.0)):
            ra, dec = m.azel_to_radec(az, el, 38.5878, -0.6714, t)
            a2, e2 = m.radec_to_azel(ra, dec, 38.5878, -0.6714, t)
            self.assertAlmostEqual(m.angle_diff(az, a2), 0.0, places=6)
            self.assertAlmostEqual(el, e2, places=6)

    def test_against_astropy(self):
        try:
            import astropy.units as u
            from astropy.coordinates import AltAz, EarthLocation, SkyCoord
            from astropy.time import Time
            from astropy.utils import iers
        except ImportError:
            self.skipTest("sin astropy")
        iers.conf.auto_download = False
        site = EarthLocation(lat=38.5878 * u.deg, lon=-0.6714 * u.deg, height=0 * u.m)
        t = 1795474800.0
        for ra, dec in ((5.9195, 7.407), (18.6156, 38.784), (2.5303, 89.264), (14.261, 19.182)):
            ref = SkyCoord(ra=ra * u.hourangle, dec=dec * u.deg, frame="icrs").transform_to(
                AltAz(obstime=Time(t, format="unix"), location=site, pressure=0 * u.hPa))
            az, el = m.radec_to_azel(ra, dec, 38.5878, -0.6714, t)
            sep = ref.separation(SkyCoord(az=az * u.deg, alt=el * u.deg, frame=ref.frame)).deg
            self.assertLess(sep, 0.02, f"{ra} {dec}: {sep:.4f}°")


if __name__ == "__main__":
    unittest.main()
