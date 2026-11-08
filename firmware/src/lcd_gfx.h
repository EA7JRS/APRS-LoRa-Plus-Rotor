/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Dibujo en memoria para la pantalla (sin Arduino: se prueba en el PC con firmware/test_host/lcd_preview.cpp).
// Puntos RGB565 guardados con los bytes ya en el orden del ST7796 (primero el alto), listos para mandarlos por SPI.
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "lcd_fonts.h"

namespace Lcd {

    // Color RGB565 en el orden de la memoria (byte alto primero)
    inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
    inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return swap16((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))); }
    inline uint16_t hex(uint32_t c) { return rgb((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c); }

    inline uint16_t blend(uint16_t bgSw, uint16_t fgSw, uint8_t a /*0-15*/) {
        if (a == 0) return bgSw;
        if (a >= 15) return fgSw;
        const uint16_t b = swap16(bgSw), f = swap16(fgSw);
        const int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
        const int fr = f >> 11, fg = (f >> 5) & 63, fb = f & 31;
        const int r = br + (fr - br) * a / 15, g = bg + (fg - bg) * a / 15, bl = bb + (fb - bb) * a / 15;
        return swap16((uint16_t)((r << 11) | (g << 5) | bl));
    }

    enum Align { LEFT, CENTER, RIGHT };

    class Fb {
    public:
        int       w = 0, h = 0;
        uint16_t* px = nullptr;

        void attach(uint16_t* buf, int width, int height) { px = buf; w = width; h = height; }

        inline void set(int x, int y, uint16_t c) { if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) px[y * w + x] = c; }
        inline uint16_t get(int x, int y) const { return px[y * w + x]; }

        void fill(uint16_t c) { for (int i = 0, n = w * h; i < n; i++) px[i] = c; }

        void fillRect(int x, int y, int rw, int rh, uint16_t c) {
            int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + rw > w ? w : x + rw, y1 = y + rh > h ? h : y + rh;
            for (int yy = y0; yy < y1; yy++) { uint16_t* p = px + yy * w; for (int xx = x0; xx < x1; xx++) p[xx] = c; }
        }
        void rect(int x, int y, int rw, int rh, uint16_t c, int t = 1) {
            fillRect(x, y, rw, t, c); fillRect(x, y + rh - t, rw, t, c);
            fillRect(x, y, t, rh, c); fillRect(x + rw - t, y, t, rh, c);
        }
        // Rectangulo con esquinas redondeadas
        void fillRound(int x, int y, int rw, int rh, int r, uint16_t c) {
            if (r * 2 > rh) r = rh / 2;
            if (r * 2 > rw) r = rw / 2;
            for (int yy = 0; yy < rh; yy++) {
                int inset = 0;
                const int dy = yy < r ? r - yy : (yy >= rh - r ? yy - (rh - r - 1) : 0);
                if (dy > 0) inset = r - (int)floor(sqrt((double)r * r - (double)dy * dy) + 0.5);
                fillRect(x + inset, y + yy, rw - 2 * inset, 1, c);
            }
        }
        void fillCircle(int cx, int cy, int r, uint16_t c) {
            for (int dy = -r; dy <= r; dy++) {
                const int dx = (int)floor(sqrt((double)r * r - (double)dy * dy) + 0.5);
                fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
            }
        }
        // Anillo con borde suavizado (radio exterior r, grosor t)
        void ring(int cx, int cy, double r, double t, uint16_t c) {
            const int R = (int)ceil(r) + 1;
            for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++) {
                const double d = sqrt((double)dx * dx + (double)dy * dy);
                double a = 0;
                if (d <= r && d >= r - t) a = 1;
                else if (d > r && d < r + 1) a = r + 1 - d;
                else if (d < r - t && d > r - t - 1) a = d - (r - t - 1);
                if (a > 0) { const int x = cx + dx, y = cy + dy; if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) px[y * w + x] = blend(px[y * w + x], c, (uint8_t)(a * 15)); }
            }
        }
        // Linea gruesa con extremos redondos y borde suavizado
        void line(double x0, double y0, double x1, double y1, double thick, uint16_t c) {
            const double hw = thick / 2;
            const int minx = (int)floor((x0 < x1 ? x0 : x1) - hw - 1), maxx = (int)ceil((x0 > x1 ? x0 : x1) + hw + 1);
            const int miny = (int)floor((y0 < y1 ? y0 : y1) - hw - 1), maxy = (int)ceil((y0 > y1 ? y0 : y1) + hw + 1);
            const double vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
            for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
                if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) continue;
                double t = len2 > 0 ? ((x - x0) * vx + (y - y0) * vy) / len2 : 0;
                t = t < 0 ? 0 : (t > 1 ? 1 : t);
                const double dx = x - (x0 + t * vx), dy = y - (y0 + t * vy);
                const double d = sqrt(dx * dx + dy * dy);
                double a = hw + 0.5 - d;
                if (a <= 0) continue;
                if (a > 1) a = 1;
                px[y * w + x] = blend(px[y * w + x], c, (uint8_t)(a * 15));
            }
        }
        // Triangulo relleno (aguja)
        void triangle(double ax, double ay, double bx, double by, double cx, double cy, uint16_t c) {
            const int minx = (int)floor(fmin(ax, fmin(bx, cx))), maxx = (int)ceil(fmax(ax, fmax(bx, cx)));
            const int miny = (int)floor(fmin(ay, fmin(by, cy))), maxy = (int)ceil(fmax(ay, fmax(by, cy)));
            auto edge = [](double x0, double y0, double x1, double y1, double x, double y) { return (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0); };
            const double area = edge(ax, ay, bx, by, cx, cy);
            if (fabs(area) < 1e-9) return;
            for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
                const double px_ = x + 0.5, py_ = y + 0.5;
                const double e0 = edge(bx, by, cx, cy, px_, py_), e1 = edge(cx, cy, ax, ay, px_, py_), e2 = edge(ax, ay, bx, by, px_, py_);
                if ((area > 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) || (area < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) set(x, y, c);
            }
        }

        // ---------------------------------------------------------------- texto (UTF-8)
        static uint32_t nextCp(const char*& s) {
            const uint8_t c = (uint8_t)*s++;
            if (c < 0x80) return c;
            if ((c & 0xE0) == 0xC0 && *s) return ((c & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
            if ((c & 0xF0) == 0xE0 && s[0] && s[1]) { const uint32_t v = ((c & 0x0F) << 12) | (((uint8_t)s[0] & 0x3F) << 6) | ((uint8_t)s[1] & 0x3F); s += 2; return v; }
            return '?';
        }
        static const LcdGlyph* glyph(const LcdFont& f, uint32_t cp) {
            int lo = 0, hi = f.n - 1;                   // tabla ordenada por punto de codigo
            while (lo <= hi) { const int m = (lo + hi) / 2; if (f.g[m].cp == cp) return &f.g[m]; if (f.g[m].cp < cp) lo = m + 1; else hi = m - 1; }
            return cp == '?' ? nullptr : glyph(f, '?');
        }
        static int textWidth(const LcdFont& f, const char* s) {
            int x = 0;
            while (*s) { const LcdGlyph* g = glyph(f, nextCp(s)); if (g) x += g->adv; }
            return x;
        }
        // y = parte de arriba de la linea. Devuelve la x final
        int text(int x, int y, const char* s, const LcdFont& f, uint16_t c, Align al = LEFT) {
            if (al == CENTER) x -= textWidth(f, s) / 2;
            else if (al == RIGHT) x -= textWidth(f, s);
            while (*s) {
                const LcdGlyph* g = glyph(f, nextCp(s));
                if (!g) continue;
                const uint8_t* b = f.bits + g->off;
                const int rowBytes = (g->w + 1) / 2;
                for (int yy = 0; yy < g->h; yy++) for (int xx = 0; xx < g->w; xx++) {
                    const uint8_t v = b[yy * rowBytes + xx / 2];
                    const uint8_t a = (xx & 1) ? (v & 15) : (v >> 4);
                    if (!a) continue;
                    const int X = x + g->x + xx, Y = y + g->y + yy;
                    if ((unsigned)X < (unsigned)w && (unsigned)Y < (unsigned)h) px[Y * w + X] = blend(px[Y * w + X], c, a);
                }
                x += g->adv;
            }
            return x;
        }
    };
}
