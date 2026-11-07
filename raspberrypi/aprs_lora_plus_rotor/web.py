# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""WebUI minima con asyncio (sin dependencias): la misma pagina que el firmware (firmware/src/web_page.h) y POST /api."""

from __future__ import annotations

import asyncio
import base64
import logging
import os

from .api import ApiSession

log = logging.getLogger("web")

_HERE = os.path.dirname(os.path.abspath(__file__))
PAGE_CANDIDATES = [os.path.join(_HERE, "web_page.h"), os.path.join(_HERE, "..", "..", "firmware", "src", "web_page.h")]


def load_page() -> bytes:
    """Extrae el HTML de web_page.h (una sola fuente para placas y Raspberry)."""
    for p in PAGE_CANDIDATES:
        try:
            with open(p, encoding="utf-8") as f:
                s = f.read()
        except OSError:
            continue
        a, b = s.find('R"HTML('), s.rfind(')HTML"')
        if a >= 0 and b > a:
            html = s[a + 7:b].replace('<a href="/update"><button>Actualizar firmware</button></a>', "")
            return html.encode()
    return b"<h1>APRS LoRa+ Rotor</h1><p>web_page.h no encontrado</p>"


class WebServer:
    def __init__(self, host, port: int, user: str, password_fn):
        self.host = host
        self.port = port
        self.user = user
        self.password_fn = password_fn      # contraseña actual ('' = sin autenticacion)
        self.page = load_page()

    async def start(self):
        self.server = await asyncio.start_server(self.handle, "0.0.0.0", self.port)
        log.info("WebUI en el puerto %d", self.port)

    def _auth_ok(self, headers: dict) -> bool:
        pw = self.password_fn()
        if not pw:
            return True
        h = headers.get("authorization", "")
        if not h.lower().startswith("basic "):
            return False
        try:
            u, _, p = base64.b64decode(h[6:]).decode().partition(":")
        except Exception:  # noqa: BLE001
            return False
        return u == self.user and p == pw

    async def handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        try:
            while True:
                req = await asyncio.wait_for(reader.readline(), 30)
                if not req:
                    break
                try:
                    method, path, _ = req.decode("latin-1").split(" ", 2)
                except ValueError:
                    break
                headers = {}
                while True:
                    line = (await asyncio.wait_for(reader.readline(), 10)).decode("latin-1").strip()
                    if not line:
                        break
                    k, _, v = line.partition(":")
                    headers[k.strip().lower()] = v.strip()
                n = int(headers.get("content-length", "0") or 0)
                if n > 65536:
                    await self._send(writer, 413, "text/plain", b"too big")
                    break
                body = await reader.readexactly(n) if n else b""
                if not self._auth_ok(headers):
                    await self._send(writer, 401, "text/plain", b"auth", {"WWW-Authenticate": 'Basic realm="APRS LoRa+ Rotor"'})
                elif method == "GET" and path.split("?")[0] == "/":
                    await self._send(writer, 200, "text/html; charset=utf-8", self.page)
                elif method == "POST" and path == "/api":
                    s = ApiSession()
                    s.authed = True
                    out: list[str] = []
                    for line in body.decode("utf-8", "replace").splitlines():
                        if line.strip():
                            out += s.handle(line, self.host)
                    await self._send(writer, 200, "application/json", ("[" + ",".join(out) + "]").encode())
                else:
                    await self._send(writer, 404, "text/plain", b"not found")
                if headers.get("connection", "").lower() == "close":
                    break
        except (asyncio.TimeoutError, asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            writer.close()

    @staticmethod
    async def _send(writer, code: int, ctype: str, body: bytes, extra: dict | None = None):
        reason = {200: "OK", 401: "Unauthorized", 404: "Not Found", 413: "Payload Too Large"}.get(code, "OK")
        h = f"HTTP/1.1 {code} {reason}\r\nContent-Type: {ctype}\r\nContent-Length: {len(body)}\r\nCache-Control: no-store\r\n"
        for k, v in (extra or {}).items():
            h += f"{k}: {v}\r\n"
        writer.write(h.encode() + b"\r\n" + body)
        await writer.drain()

