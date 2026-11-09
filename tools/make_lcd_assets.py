#!/usr/bin/env python3
# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Recursos de la pantalla (placas con LCD):
#  - firmware/src/lcd_fonts.h: glifos de DejaVu Sans (licencia Bitstream Vera / DejaVu, ver firmware/src/NOTICE_FONTS.md),
#    4 bits por punto (16 niveles, se mezclan con el fondo), recortados a su caja.
#  - firmware/src/lcd_logo.h: el logo de la cabecera de la WebUI (el icono de la app Android, PNG dentro de web_page.h) en RGB565
#    sobre el rojo de la cabecera.
#  - firmware/src/lcd_splash.h: pantalla de arranque, la de bienvenida de la app Android (tools/assets/welcome_logo.jpg, copia de
#    app/src/main/res/drawable/welcome_logo.jpg de APRS-LoRa-Plus-Android) en 256 colores (paleta RGB565 + un byte por punto).
#
#   python3 tools/make_lcd_assets.py [--dejavu /usr/share/fonts/truetype/dejavu]
import argparse
import base64
import io
import os
import re

from PIL import Image, ImageDraw, ImageFont

ASCII = [chr(c) for c in range(32, 127)]
EXTRA = list("°·©ñÑáéíóúÁÉÍÓÚü¿¡±→×●−…↑↓")

# nombre, fichero, tamaño en px, caracteres
FONTS = [
    ("FontS", "DejaVuSans.ttf", 15, ASCII + EXTRA),
    ("FontM", "DejaVuSans-Bold.ttf", 20, ASCII + EXTRA),
    ("FontL", "DejaVuSans-Bold.ttf", 38, list(" 0123456789.-°AZEL")),
    # Pantallas apaisadas y con más puntos por pulgada (AMOLED de 2,41", 600 x 450)
    ("FontS2", "DejaVuSans.ttf", 22, ASCII + EXTRA),
    ("FontM2", "DejaVuSans-Bold.ttf", 28, ASCII + EXTRA),
    ("FontL2", "DejaVuSans-Bold.ttf", 54, list(" 0123456789.-°AZEL")),
    # pantallas de 1024 x 600 (ESP32-S3-Touch-LCD-7B)
    ("FontS3", "DejaVuSans.ttf", 29, ASCII + EXTRA),
    ("FontM3", "DejaVuSans-Bold.ttf", 37, ASCII + EXTRA),
    ("FontL3", "DejaVuSans-Bold.ttf", 72, list(" 0123456789.-°AZEL")),
]


def build(name, path, size, chars):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    glyphs = []
    data = bytearray()
    for ch in chars:
        adv = int(round(font.getlength(ch)))
        img = Image.new("L", (adv + size, height + size), 0)
        ImageDraw.Draw(img).text((size // 2, 0), ch, font=font, fill=255)
        bbox = img.getbbox()
        if bbox is None:
            glyphs.append((ord(ch), len(data), 0, 0, 0, 0, adv))
            continue
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        off = len(data)
        row = bytearray()
        for y in range(y0, y1):
            nib = []
            for x in range(x0, x1):
                nib.append(img.getpixel((x, y)) >> 4)
            if len(nib) % 2:
                nib.append(0)
            for i in range(0, len(nib), 2):
                row.append((nib[i] << 4) | nib[i + 1])
        data += row
        glyphs.append((ord(ch), off, w, h, x0 - size // 2, y0, adv))
    return height, ascent, glyphs, data


HEADER_RGB = (0xB2, 0x1D, 0x1D)


def rgb565_sw(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v << 8) | (v >> 8)) & 0xFFFF


def make_logo(out, h):
    page = open("firmware/src/web_page.h", encoding="utf-8").read()
    b64 = re.search(r'<img class="logo" src="data:image/png;base64,([A-Za-z0-9+/=]+)"', page).group(1)
    img = Image.open(io.BytesIO(base64.b64decode(b64))).convert("RGBA")
    img = img.resize((h * img.size[0] // img.size[1], h), Image.LANCZOS)
    bg = Image.new("RGBA", img.size, HEADER_RGB + (255,))
    img = Image.alpha_composite(bg, img).convert("RGB")
    w, h = img.size
    vals = [rgb565_sw(*img.getpixel((x, y))) for y in range(h) for x in range(w)]
    lines = [
        "/* Copyright (C) 2026 Manuel - EA7JRS",
        " *",
        " * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).",
        " * Logo APRS LoRa+ (el de la WebUI) generado por tools/make_lcd_assets.py. No editar a mano.",
        " */",
        "",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "static const int LCD_LOGO_W = %d, LCD_LOGO_H = %d;" % (w, h),
        "static const uint16_t LCD_LOGO[] = {   // RGB565, byte alto primero, sobre el rojo de la cabecera",
    ]
    for i in range(0, len(vals), 16):
        lines.append("    " + ",".join("0x%04x" % v for v in vals[i:i + 16]) + ",")
    lines.append("};")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("written", out, w, "x", h)


SPLASH_BG = (0x13, 0x1A, 0x22)          # fondo de la pantalla de bienvenida de la app


def feather(img, bg, frac=0.16):
    """Funde los bordes de la imagen con el fondo (la de la app tiene un halo algo mas claro que se veria como un recuadro)."""
    w, h = img.size
    m = Image.new("L", (w, h))
    bw = max(1, int(min(w, h) * frac))
    px = m.load()
    for y in range(h):
        for x in range(w):
            d = min(x, y, w - 1 - x, h - 1 - y) / bw
            t = 1.0 if d >= 1 else d * d * (3 - 2 * d)
            px[x, y] = int(255 * t)
    return Image.composite(img, Image.new("RGB", (w, h), bg), m)


def make_splash(src, out, h):
    img = Image.open(src).convert("RGB")
    w = round(img.size[0] * h / img.size[1])
    img = img.resize((w, h), Image.LANCZOS)
    img = feather(img, SPLASH_BG)
    q = img.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    pal = q.getpalette()[:768]
    ncol = len(pal) // 3
    idx = q.tobytes()
    lines = [
        "/* Copyright (C) 2026 Manuel - EA7JRS",
        " *",
        " * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).",
        " * Pantalla de arranque (la de bienvenida de la app APRS LoRa+) generada por tools/make_lcd_assets.py. No editar a mano.",
        " */",
        "",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "static const int LCD_SPLASH_W = %d, LCD_SPLASH_H = %d;" % (w, h),
        "static const uint32_t LCD_SPLASH_BG = 0x%02x%02x%02x;" % SPLASH_BG,
        "static const uint16_t LCD_SPLASH_PAL[256] = {   // RGB565, byte alto primero",
    ]
    vals = [rgb565_sw(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]) for i in range(ncol)] + [0] * (256 - ncol)
    for i in range(0, 256, 16):
        lines.append("    " + ",".join("0x%04x" % v for v in vals[i:i + 16]) + ",")
    lines.append("};")
    lines.append("static const uint8_t LCD_SPLASH[] = {   // un byte por punto (indice de LCD_SPLASH_PAL), por filas")
    for i in range(0, len(idx), 32):
        lines.append("    " + ",".join(str(b) for b in idx[i:i + 32]) + ",")
    lines.append("};")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("written", out, w, "x", h)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dejavu", default="/usr/share/fonts/truetype/dejavu")
    ap.add_argument("--out", default="firmware/src/lcd_fonts.h")
    ap.add_argument("--logo-out", default="firmware/src/lcd_logo.h")
    ap.add_argument("--logo-h", type=int, default=40)
    ap.add_argument("--splash-src", default="tools/assets/welcome_logo.jpg")
    ap.add_argument("--splash-out", default="firmware/src/lcd_splash.h")
    ap.add_argument("--splash-h", type=int, default=400)      # cabe en 320 x 480 y en 600 x 450 con el texto debajo
    a = ap.parse_args()
    make_logo(a.logo_out, a.logo_h)
    make_splash(a.splash_src, a.splash_out, a.splash_h)
    out = [
        "/* Copyright (C) 2026 Manuel - EA7JRS",
        " *",
        " * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).",
        " * Glifos generados por tools/make_lcd_fonts.py a partir de DejaVu Sans (ver NOTICE_FONTS.md). No editar a mano.",
        " */",
        "",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "struct LcdGlyph { uint16_t cp; uint32_t off; uint8_t w, h; int8_t x, y; uint8_t adv; };",
        "struct LcdFont { const LcdGlyph* g; uint16_t n; const uint8_t* bits; uint8_t height, ascent; };",
        "",
    ]
    for name, file, size, chars in FONTS:
        height, ascent, glyphs, data = build(name, os.path.join(a.dejavu, file), size, chars)
        out.append("// %s: %s %d px, %d glifos, %d bytes" % (name, file, size, len(glyphs), len(data)))
        out.append("static const uint8_t %s_bits[] = {" % name)
        for i in range(0, len(data), 24):
            out.append("    " + ",".join("0x%02x" % b for b in data[i:i + 24]) + ",")
        out.append("};")
        out.append("static const LcdGlyph %s_glyphs[] = {" % name)
        for cp, off, w, h, x, y, adv in sorted(glyphs):
            out.append("    {%d,%d,%d,%d,%d,%d,%d}," % (cp, off, w, h, x, y, adv))
        out.append("};")
        out.append("static const LcdFont %s = { %s_glyphs, %d, %s_bits, %d, %d };" % (name, name, len(glyphs), name, height, ascent))
        out.append("")
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print("written", a.out)


if __name__ == "__main__":
    main()
