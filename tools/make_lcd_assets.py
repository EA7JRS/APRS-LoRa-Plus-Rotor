#!/usr/bin/env python3
# Copyright (C) 2026 Manuel - EA7JRS
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Recursos de la pantalla (placas con LCD):
#  - firmware/src/lcd_fonts.h: glifos de DejaVu Sans (licencia Bitstream Vera / DejaVu, ver firmware/src/NOTICE_FONTS.md),
#    4 bits por punto (16 niveles, se mezclan con el fondo), recortados a su caja.
#  - firmware/src/lcd_logo.h: el logo de la cabecera de la WebUI (SVG de web_page.h) en RGB565 sobre el rojo de la cabecera.
#    Necesita rsvg-convert (librsvg).
#
#   python3 tools/make_lcd_assets.py [--dejavu /usr/share/fonts/truetype/dejavu]
import argparse
import io
import os
import re
import subprocess

from PIL import Image, ImageDraw, ImageFont

ASCII = [chr(c) for c in range(32, 127)]
EXTRA = list("°·©ñÑáéíóúÁÉÍÓÚü¿¡±→×●−…")

# nombre, fichero, tamaño en px, caracteres
FONTS = [
    ("FontS", "DejaVuSans.ttf", 15, ASCII + EXTRA),
    ("FontM", "DejaVuSans-Bold.ttf", 20, ASCII + EXTRA),
    ("FontL", "DejaVuSans-Bold.ttf", 38, list(" 0123456789.-°AZEL")),
    # Pantallas apaisadas y con más puntos por pulgada (AMOLED de 2,41", 600 x 450)
    ("FontS2", "DejaVuSans.ttf", 22, ASCII + EXTRA),
    ("FontM2", "DejaVuSans-Bold.ttf", 28, ASCII + EXTRA),
    ("FontL2", "DejaVuSans-Bold.ttf", 54, list(" 0123456789.-°AZEL")),
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
    svg = re.search(r'<svg class="logo".*?</svg>', page, re.S).group(0)
    svg = svg.replace('class="logo" ', "")
    png = subprocess.run(["rsvg-convert", "-h", str(h)], input=svg.encode(), capture_output=True, check=True).stdout
    img = Image.open(io.BytesIO(png)).convert("RGBA")
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dejavu", default="/usr/share/fonts/truetype/dejavu")
    ap.add_argument("--out", default="firmware/src/lcd_fonts.h")
    ap.add_argument("--logo-out", default="firmware/src/lcd_logo.h")
    ap.add_argument("--logo-h", type=int, default=40)
    a = ap.parse_args()
    make_logo(a.logo_out, a.logo_h)
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
