# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Bluetooth en la Raspberry Pi por D-Bus de BlueZ (paquete python3-dbus-next):

* BLE: el mismo servicio GATT que las placas (UUID 4c6f5261-2b00-4ea7-b000-000000000001) con el protocolo JSON (app).
  Enlace cifrado con emparejamiento "Just Works" y PIN de control en la orden "auth".
* SPP (Bluetooth clasico): perfil Serial Port con un protocolo de Hamlib (GS-232B por defecto) para el tracker APRS LoRa+,
  que se conecta por nombre. El adaptador queda visible con el nombre del rotor.
"""

from __future__ import annotations

import asyncio
import logging
import os
import socket

log = logging.getLogger("bluez")

try:
    from dbus_next import BusType, Variant
    from dbus_next.aio import MessageBus
    from dbus_next.constants import PropertyAccess
    from dbus_next.service import ServiceInterface, dbus_property, method
    HAVE_DBUS = True
except ImportError:          # sin dbus-next: el demonio sigue sin Bluetooth
    HAVE_DBUS = False

SVC_UUID = "4c6f5261-2b00-4ea7-b000-000000000001"
CMD_UUID = "4c6f5261-2b00-4ea7-b000-000000000002"
EVT_UUID = "4c6f5261-2b00-4ea7-b000-000000000003"
SPP_UUID = "00001101-0000-1000-8000-00805f9b34fb"

APP_PATH = "/es/ea7jrs/rotor"

if HAVE_DBUS:

    class Agent(ServiceInterface):
        """Agente sin E/S: emparejamientos "Just Works" (el control lo protege el PIN de la orden auth)."""

        def __init__(self):
            super().__init__("org.bluez.Agent1")

        @method()
        def Release(self): pass

        @method()
        def RequestAuthorization(self, device: "o"): pass  # noqa: F821

        @method()
        def AuthorizeService(self, device: "o", uuid: "s"): pass  # noqa: F821

        @method()
        def RequestConfirmation(self, device: "o", passkey: "u"): pass  # noqa: F821

        @method()
        def Cancel(self): pass

    class Advertisement(ServiceInterface):
        def __init__(self, name: str):
            super().__init__("org.bluez.LEAdvertisement1")
            self.name = name

        @dbus_property(access=PropertyAccess.READ)
        def Type(self) -> "s": return "peripheral"  # noqa: F821

        @dbus_property(access=PropertyAccess.READ)
        def ServiceUUIDs(self) -> "as": return [SVC_UUID]  # noqa: F821,F722

        @dbus_property(access=PropertyAccess.READ)
        def LocalName(self) -> "s": return self.name  # noqa: F821

        @dbus_property(access=PropertyAccess.READ)
        def Includes(self) -> "as": return ["tx-power"]  # noqa: F821,F722

        @method()
        def Release(self): pass

    class GattService(ServiceInterface):
        def __init__(self):
            super().__init__("org.bluez.GattService1")

        @dbus_property(access=PropertyAccess.READ)
        def UUID(self) -> "s": return SVC_UUID  # noqa: F821

        @dbus_property(access=PropertyAccess.READ)
        def Primary(self) -> "b": return True  # noqa: F821

    class GattChar(ServiceInterface):
        def __init__(self, uuid: str, flags: list[str], on_write=None):
            super().__init__("org.bluez.GattCharacteristic1")
            self.uuid = uuid
            self.flags = flags
            self.on_write = on_write
            self.value = b""
            self.notifying = False

        @dbus_property(access=PropertyAccess.READ)
        def UUID(self) -> "s": return self.uuid  # noqa: F821

        @dbus_property(access=PropertyAccess.READ)
        def Service(self) -> "o": return APP_PATH + "/service0"  # noqa: F821

        @dbus_property(access=PropertyAccess.READ)
        def Flags(self) -> "as": return self.flags  # noqa: F821,F722

        @dbus_property(access=PropertyAccess.READ)
        def Value(self) -> "ay": return self.value  # noqa: F821

        @method()
        def ReadValue(self, options: "a{sv}") -> "ay": return self.value  # noqa: F821,F722

        @method()
        def WriteValue(self, value: "ay", options: "a{sv}"):  # noqa: F821,F722
            if self.on_write:
                self.on_write(bytes(value))

        @method()
        def StartNotify(self): self.notifying = True

        @method()
        def StopNotify(self): self.notifying = False

        def notify(self, data: bytes):
            self.value = data
            if self.notifying:
                self.emit_properties_changed({"Value": data})

    class ObjectManager(ServiceInterface):
        def __init__(self, objects: dict):
            super().__init__("org.freedesktop.DBus.ObjectManager")
            self.objects = objects    # ruta -> (interfaz, {propiedad: Variant})

        @method()
        def GetManagedObjects(self) -> "a{oa{sa{sv}}}":  # noqa: F821,F722
            return {path: {iface: props} for path, (iface, props) in self.objects.items()}

    class SppProfile(ServiceInterface):
        def __init__(self, on_conn):
            super().__init__("org.bluez.Profile1")
            self.on_conn = on_conn

        @method()
        def Release(self): pass

        @method()
        def NewConnection(self, device: "o", fd: "h", props: "a{sv}"):  # noqa: F821,F722
            self.on_conn(device, os.dup(fd))

        @method()
        def RequestDisconnection(self, device: "o"): pass  # noqa: F821


class Bluetooth:
    """BLE (protocolo JSON) y SPP (protocolo de Hamlib). make_api() -> sesion con handle(linea) / tick();
    make_port() -> objeto con feed(bytes, rotor) -> bytes."""

    def __init__(self, cfg: dict, name: str, rotor, api_host, make_session, make_port):
        self.cfg = cfg
        self.name = name
        self.rotor = rotor
        self.api_host = api_host
        self.make_session = make_session
        self.make_port = make_port
        self.bus = None
        self.evt = None
        self.session = None
        self.inbuf = b""
        self.ble_clients = 0
        self.spp_clients = 0

    async def start(self):
        if not HAVE_DBUS:
            log.warning("python3-dbus-next no esta instalado: sin Bluetooth")
            return
        self.bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
        hci = "/org/bluez/" + self.cfg.get("adapter", "hci0")
        intro = await self.bus.introspect("org.bluez", hci)
        adapter = self.bus.get_proxy_object("org.bluez", hci, intro)
        props = adapter.get_interface("org.freedesktop.DBus.Properties")
        await props.call_set("org.bluez.Adapter1", "Powered", Variant("b", True))
        await props.call_set("org.bluez.Adapter1", "Alias", Variant("s", self.name))
        await props.call_set("org.bluez.Adapter1", "Pairable", Variant("b", True))
        # Agente "Just Works"
        agent = Agent()
        self.bus.export(APP_PATH + "/agent", agent)
        aintro = await self.bus.introspect("org.bluez", "/org/bluez")
        am = self.bus.get_proxy_object("org.bluez", "/org/bluez", aintro).get_interface("org.bluez.AgentManager1")
        await am.call_register_agent(APP_PATH + "/agent", "NoInputNoOutput")
        await am.call_request_default_agent(APP_PATH + "/agent")

        if self.cfg.get("ble", True):
            await self._start_ble(adapter)
        if self.cfg.get("spp", False):
            await props.call_set("org.bluez.Adapter1", "DiscoverableTimeout", Variant("u", 0))
            await props.call_set("org.bluez.Adapter1", "Discoverable", Variant("b", True))
            await self._start_spp(aintro)

    # ---- BLE -------------------------------------------------------------------------------------------------------------
    async def _start_ble(self, adapter):
        svc = GattService()
        cmd = GattChar(CMD_UUID, ["write", "write-without-response", "encrypt-write"], self._on_ble_write)
        self.evt = GattChar(EVT_UUID, ["notify", "read", "encrypt-read"])
        self.evt.value = b'{"e":"hello","dev":"APRS LoRa+ Rotor"}\n'
        paths = {APP_PATH + "/service0": svc, APP_PATH + "/service0/char0": cmd, APP_PATH + "/service0/char1": self.evt}
        objs = {}
        for p, o in paths.items():
            self.bus.export(p, o)
        objs[APP_PATH + "/service0"] = ("org.bluez.GattService1", {"UUID": Variant("s", SVC_UUID), "Primary": Variant("b", True)})
        for p, ch in ((APP_PATH + "/service0/char0", cmd), (APP_PATH + "/service0/char1", self.evt)):
            objs[p] = ("org.bluez.GattCharacteristic1", {"UUID": Variant("s", ch.uuid),
                                                          "Service": Variant("o", APP_PATH + "/service0"),
                                                          "Flags": Variant("as", ch.flags)})
        self.bus.export(APP_PATH, ObjectManager(objs))
        gm = adapter.get_interface("org.bluez.GattManager1")
        await gm.call_register_application(APP_PATH, {})
        adv = Advertisement(self.name)
        self.bus.export(APP_PATH + "/adv0", adv)
        lam = adapter.get_interface("org.bluez.LEAdvertisingManager1")
        await lam.call_register_advertisement(APP_PATH + "/adv0", {})
        self.session = self.make_session()
        asyncio.create_task(self._ble_ticker())
        log.info("BLE listo: %s", self.name)

    def _on_ble_write(self, data: bytes):
        if self.session is None:
            return
        self.inbuf += data
        while b"\n" in self.inbuf:
            line, self.inbuf = self.inbuf.split(b"\n", 1)
            if line.strip():
                for l in self.session.handle(line.decode("utf-8", "replace"), self.api_host):
                    self._ble_send(l)
        if len(self.inbuf) > 1024:
            self.inbuf = b""

    def _ble_send(self, line: str):
        if not self.evt:
            return
        data = (line + "\n").encode()
        for i in range(0, len(data), 180):          # BlueZ trocea segun el MTU negociado
            self.evt.notify(data[i:i + 180])

    async def _ble_ticker(self):
        while True:
            await asyncio.sleep(0.2)
            if self.evt and self.evt.notifying and self.session:
                for l in self.session.tick(self.api_host):
                    self._ble_send(l)
            elif self.evt and not self.evt.notifying and self.session and self.session.authed:
                self.session = self.make_session()          # el movil se fue: sesion nueva (PIN otra vez)

    # ---- SPP -------------------------------------------------------------------------------------------------------------
    async def _start_spp(self, aintro):
        prof = SppProfile(self._on_spp)
        self.bus.export(APP_PATH + "/spp", prof)
        pm = self.bus.get_proxy_object("org.bluez", "/org/bluez", aintro).get_interface("org.bluez.ProfileManager1")
        await pm.call_register_profile(APP_PATH + "/spp", SPP_UUID, {
            "Name": Variant("s", "APRS LoRa+ Rotor"), "Role": Variant("s", "server"), "Channel": Variant("q", 1),
            "RequireAuthentication": Variant("b", False), "RequireAuthorization": Variant("b", False),
            "AutoConnect": Variant("b", True)})
        log.info("SPP listo: %s (canal 1)", self.name)

    def _on_spp(self, device: str, fd: int):
        log.info("SPP: conexion de %s", device)
        asyncio.create_task(self._spp_conn(fd))

    async def _spp_conn(self, fd: int):
        sock = socket.socket(fileno=fd)
        sock.setblocking(False)
        reader, writer = await asyncio.open_connection(sock=sock)
        port = self.make_port()
        self.spp_clients += 1
        try:
            while True:
                data = await reader.read(256)
                if not data:
                    break
                out = port.feed(data, self.rotor)
                if out:
                    writer.write(out)
                    await writer.drain()
        except (ConnectionError, OSError):
            pass
        finally:
            self.spp_clients -= 1
            writer.close()
