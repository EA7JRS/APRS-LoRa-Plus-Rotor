# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
# This file is part of APRS LoRa+ Rotor.
"""Puerto serie con asyncio y termios (sin pyserial)."""

from __future__ import annotations

import asyncio
import os
import termios
import tty

BAUDS = {1200: termios.B1200, 2400: termios.B2400, 4800: termios.B4800, 9600: termios.B9600, 19200: termios.B19200,
         38400: termios.B38400, 57600: termios.B57600, 115200: termios.B115200}


class SerialPort:
    def __init__(self, device: str, baud: int):
        self.device = device
        self.baud = baud
        self.fd = -1
        self.queue: asyncio.Queue[bytes] = asyncio.Queue()

    def open(self):
        self.fd = os.open(self.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        a = termios.tcgetattr(self.fd)
        sp = BAUDS.get(int(self.baud), termios.B9600)
        a[4] = a[5] = sp
        a[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        asyncio.get_running_loop().add_reader(self.fd, self._readable)

    def _readable(self):
        try:
            data = os.read(self.fd, 512)
        except (BlockingIOError, InterruptedError):
            return
        except OSError:
            data = b""
        self.queue.put_nowait(data)

    async def read(self) -> bytes:
        return await self.queue.get()      # b"" = puerto perdido

    def write(self, data: bytes):
        if self.fd >= 0 and data:
            try:
                os.write(self.fd, data)
            except OSError:
                pass

    def close(self):
        if self.fd >= 0:
            try:
                asyncio.get_running_loop().remove_reader(self.fd)
            except RuntimeError:
                pass
            os.close(self.fd)
            self.fd = -1
