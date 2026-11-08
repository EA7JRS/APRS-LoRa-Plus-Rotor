/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Pantallas de las placas con pantalla táctil: mando del rotor e información / acerca de.
// Dos disposiciones: vertical 320 x 480 (Waveshare ESP32-C5-Touch-LCD-3.5) y apaisada 600 x 450 (ESP32-S3-Touch-AMOLED-2.41).
// Sin Arduino: dibuja en un Lcd::Fb y traduce los toques en órdenes. lcd_hw.cpp lo une al hardware y al rotor;
// firmware/test_host/lcd_preview.cpp lo dibuja en el PC.
#include <stdio.h>
#include <cmath>
#include <string>
#include <vector>
#include "lcd_gfx.h"
#include "lcd_logo.h"

namespace Lcd {

    struct UiState {
        // Rotor
        double      az = NAN, el = NAN;             // posición
        double      taz = NAN, tel = NAN;           // objetivo
        double      maz = NAN;                      // acimut mecánico (solape > 360)
        double      minAz = 0, maxAz = 360;
        bool        hasEl = true;
        bool        moving = false;
        bool        canMove = true;                 // Rot2Prog no admite mover a mano
        uint8_t     fault = 0;
        std::string faultText = "OK";
        std::string aprs;                           // última orden APRS
        // Equipo
        std::string ver, name, call, board, chip, drv, ip, wifi;
        int         rotctld = 0, apiPort = 0;
        bool        ble = false, bleConn = false;
        uint32_t    upS = 0;
        int         bright = 80;
    };

    enum class Cmd { NONE, MOVE, STOP, PARK, GOTO, CLEAR_FAULT, BRIGHT };

    struct UiCmd {
        Cmd    cmd = Cmd::NONE;
        int    dir = 0;          // MOVE: direcciones RC::MOVE_* (2 UP, 4 DOWN, 8 CCW, 16 CW)
        double az = NAN;         // GOTO
        int    value = 0;        // BRIGHT: nuevo brillo (%)
    };

    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool in(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };

    // Medidas de cada disposición
    struct Layout {
        bool          land = false;
        int           W = 320, H = 480;
        const LcdFont *S = &FontS, *M = &FontM, *L = &FontL;
        double        k = 1;                         // escala de grosores y flechas
        int           hdrH = 48, titleX = 56, titleY = 3, subY = 27, logoX = 10, logoY = 4;
        int           infoX = 292, infoY = 24, infoR = 16, linkX = 262, link1Y = 6, link2Y = 26;
        int           cx = 160, cy = 154, r = 98, tickLabelR = 111;
        // Lecturas: etiqueta y número (centro) de AZ y EL; con un solo eje, otra posición del AZ
        int           azLx = 6, azLy = 286, azNx = 88, azNy = 282, elLx = 166, elLy = 286, elNx = 248, elNy = 282;
        int           az1Lx = 60, az1Ly = 286, az1Nx = 165, az1Ny = 282;
        Rect          divider = { 160, 284, 1, 40 };
        Rect          status = { 8, 332, 304, 24 };
        int           statusLines = 1;
        Rect          btn[6];
        bool          stacked = false;               // icono arriba y texto abajo
        // Información
        Rect          infoCard = { 8, 54, 304, 222 }, aboutCard = { 8, 282, 304, 138 };
        int           infoValX = 118, lineH = 21;
        Rect          brMinus = { 16, 426, 80, 48 }, brPlus = { 224, 426, 80, 48 };
        int           brTextX = 160, brTextY = 441;

        static Layout make(int w, int h) {
            Layout l;
            l.W = w; l.H = h;
            if (w <= h) {                            // vertical 320 x 480: rejilla de 3 x 2 botones abajo
                const int gap = 6, bw = (w - 4 * gap) / 3, bh = 56, y0 = 362;
                for (int i = 0; i < 6; i++) l.btn[i] = { gap + (i % 3) * (bw + gap), y0 + (i / 3) * (bh + gap), bw, bh };
                return l;
            }
            // Apaisada 600 x 450: brújula a la izquierda, lecturas a la derecha, fila de 6 botones abajo
            l.land = true;
            l.S = &FontS2; l.M = &FontM2; l.L = &FontL2; l.k = 1.4;
            l.hdrH = 56; l.titleX = 58; l.titleY = 0; l.subY = 31; l.logoX = 10; l.logoY = 8;
            l.infoX = 570; l.infoY = 28; l.infoR = 20; l.linkX = 536; l.link1Y = 2; l.link2Y = 28;
            l.cx = 162; l.cy = 207; l.r = 116; l.tickLabelR = 133;
            l.azLx = 312; l.azLy = 84;  l.azNx = 470; l.azNy = 66;
            l.elLx = 312; l.elLy = 160; l.elNx = 470; l.elNy = 142;
            l.az1Lx = 312; l.az1Ly = 122; l.az1Nx = 470; l.az1Ny = 104;
            l.divider = { 312, 136, 280, 1 };
            l.status = { 304, 218, 292, 64 };
            l.statusLines = 2;
            l.stacked = true;
            const int gap = 6, bw = (w - 7 * gap) / 6, bh = 82, y0 = h - bh - 6;
            for (int i = 0; i < 6; i++) l.btn[i] = { gap + i * (bw + gap), y0, bw, bh };
            l.infoCard = { 8, 64, 300, 380 }; l.aboutCard = { 316, 64, 276, 296 };
            l.infoValX = 130; l.lineH = 33;
            l.brMinus = { 336, 368, 72, 72 }; l.brPlus = { 512, 368, 72, 72 };
            l.brTextX = 460; l.brTextY = 392;
            return l;
        }
    };

    class Ui {
    public:
        explicit Ui(int w = 320, int h = 480) : L(Layout::make(w, h)) {}
        const Layout L;
        enum Page { MAIN, INFO };
        Page page = MAIN;

        // Colores (los de la WebUI)
        const uint16_t BG = hex(0x10151c), CARD = hex(0x19212b), FG = hex(0xe6edf3), MUT = hex(0x8b98a5), ACC = hex(0x4aa3ff);
        const uint16_t RED = hex(0xff5d5d), HDR = hex(0xb21d1d), BTN = hex(0x263241), BTN_ON = hex(0x4aa3ff);
        const uint16_t LINE = hex(0x33475c), GREEN = hex(0x3fb950), WHITE = hex(0xffffff), STOPC = hex(0xd63a3a);

        // ------------------------------------------------------------------------------------------------- dibujo
        void draw(Fb& fb, const UiState& s) {
            fb.fill(BG);
            header(fb, s);
            if (page == MAIN) mainPage(fb, s); else infoPage(fb, s);
        }

        // ------------------------------------------------------------------------------------------------- toques
        // down: dedo en la pantalla (x, y válidos); al levantarlo, down = false. Llamar con cada lectura del táctil.
        UiCmd touch(bool down, int x, int y, uint32_t nowMs, const UiState& s) {
            UiCmd c;
            if (down && !pressed_) {                                   // empieza un toque
                pressed_ = true; startMs_ = nowMs; held_ = -1; aiming_ = false; moveSent_ = false;
                const int dx = x - L.infoX, dy = y - L.infoY, rr = L.infoR + 14;
                if (dx * dx + dy * dy <= rr * rr) { page = page == MAIN ? INFO : MAIN; return c; }
                if (page == MAIN) {
                    const int b = buttonAt(x, y);
                    if (b >= 0) {
                        held_ = b;
                        switch (b) {
                            case 0: if (s.canMove) { c.cmd = Cmd::MOVE; c.dir = 8; } break;
                            case 1: c.cmd = Cmd::STOP; break;
                            case 2: if (s.canMove) { c.cmd = Cmd::MOVE; c.dir = 16; } break;
                            case 3: if (s.canMove && s.hasEl) { c.cmd = Cmd::MOVE; c.dir = 4; } break;
                            case 4: c.cmd = Cmd::PARK; break;
                            case 5: if (s.canMove && s.hasEl) { c.cmd = Cmd::MOVE; c.dir = 2; } break;
                        }
                        moveSent_ = c.cmd == Cmd::MOVE;
                        return c;
                    }
                    if (inCompass(x, y)) { aiming_ = true; aimAz_ = bearing(x, y); return c; }
                    if (s.fault && L.status.in(x, y)) { c.cmd = Cmd::CLEAR_FAULT; return c; }
                } else {
                    if (L.brMinus.in(x, y)) { c.cmd = Cmd::BRIGHT; c.value = s.bright - 10 < 10 ? 10 : s.bright - 10; return c; }
                    if (L.brPlus.in(x, y))  { c.cmd = Cmd::BRIGHT; c.value = s.bright + 10 > 100 ? 100 : s.bright + 10; return c; }
                }
                return c;
            }
            if (down && pressed_) {                                    // arrastrando
                if (aiming_ && inCompass(x, y)) aimAz_ = bearing(x, y);
                return c;
            }
            if (!down && pressed_) {                                   // se levanta
                pressed_ = false;
                if (moveSent_) { moveSent_ = false; c.cmd = Cmd::STOP; }
                else if (aiming_ && nowMs - startMs_ >= AIM_HOLD_MS) { c.cmd = Cmd::GOTO; c.az = aimAz_; }
                aiming_ = false; held_ = -1;
            }
            return c;
        }

        bool aiming(uint32_t nowMs) const { return aiming_ && pressed_ && nowMs - startMs_ >= AIM_HOLD_MS; }
        bool busyTouch() const { return pressed_; }
        void setFrameTime(uint32_t ms) { frameMs_ = ms; }

        // Texto partido en líneas que caben en w (corta por espacios y '/'); devuelve las líneas
        static std::vector<std::string> wrap(const LcdFont& f, const std::string& t, int w) {
            std::vector<std::string> out;
            std::string line, word;
            auto flushWord = [&]() {
                if (word.empty()) return;
                std::string cand = line + word, trimmed = cand;
                while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
                if (!line.empty() && Fb::textWidth(f, trimmed.c_str()) > w) {
                    while (!line.empty() && line.back() == ' ') line.pop_back();
                    out.push_back(line); line = word;
                } else line = cand;
                word.clear();
            };
            for (char ch : t) { word += ch; if (ch == ' ' || ch == '/') flushWord(); }
            flushWord();
            if (!line.empty()) out.push_back(line);
            return out;
        }
        // Recorta con "…" si no cabe
        static std::string fit(const LcdFont& f, std::string t, int w) {
            if (Fb::textWidth(f, t.c_str()) <= w) return t;
            while (!t.empty()) {
                do { t.pop_back(); } while (!t.empty() && ((uint8_t)t.back() & 0xC0) == 0x80);   // UTF-8 entero
                const std::string c = t + "…";
                if (Fb::textWidth(f, c.c_str()) <= w) return c;
            }
            return "…";
        }

    private:
        static constexpr uint32_t AIM_HOLD_MS = 500;
        bool     pressed_ = false, moveSent_ = false, aiming_ = false;
        int      held_ = -1;
        uint32_t startMs_ = 0, frameMs_ = 0;
        double   aimAz_ = 0;

        bool inCompass(int x, int y) const { const int dx = x - L.cx, dy = y - L.cy, rr = L.r + 18; return dx * dx + dy * dy <= rr * rr; }
        double bearing(int x, int y) const {
            double a = atan2((double)(x - L.cx), (double)(L.cy - y)) * 180.0 / M_PI;
            if (a < 0) a += 360;
            return round(a);
        }
        int buttonAt(int x, int y) const { for (int i = 0; i < 6; i++) if (L.btn[i].in(x, y)) return i; return -1; }

        void header(Fb& fb, const UiState& s) {
            fb.fillRect(0, 0, L.W, L.hdrH, HDR);
            fb.fillRect(0, L.hdrH - 1, L.W, 1, hex(0x7a1414));
            for (int y = 0; y < LCD_LOGO_H; y++) for (int x = 0; x < LCD_LOGO_W; x++) fb.set(L.logoX + x, L.logoY + y, LCD_LOGO[y * LCD_LOGO_W + x]);
            fb.text(L.titleX, L.titleY, "APRS LoRa+", *L.M, WHITE);
            const std::string sub = "Rotor " + s.ver;
            fb.text(L.titleX + 1, L.subY, sub.c_str(), *L.S, hex(0xf0d0d0));
            // Botón de información (i) / volver (×)
            fb.ring(L.infoX, L.infoY, L.infoR, 2 * L.k, WHITE);
            fb.text(L.infoX, L.infoY - L.M->height / 2 + 1, page == MAIN ? "i" : "×", *L.M, WHITE, CENTER);
            // Enlaces: WiFi y BLE
            fb.text(L.linkX, L.link1Y, "WiFi", *L.S, s.ip.empty() ? hex(0x6d2a2a) : WHITE, RIGHT);
            if (s.ble) fb.text(L.linkX, L.link2Y, s.bleConn ? "BLE ●" : "BLE", *L.S, s.bleConn ? WHITE : hex(0xe6a0a0), RIGHT);
        }

        void needle(Fb& fb, double az, double len, double thick, uint16_t c, bool tip) {
            const double k = az * M_PI / 180.0, sx = sin(k), cy = -cos(k), t = L.k;
            const double ex = L.cx + sx * len, ey = L.cy + cy * len;
            fb.line(L.cx, L.cy, ex - sx * (tip ? 10 * t : 0), ey - cy * (tip ? 10 * t : 0), thick, c);
            if (tip) {
                const double px = -cy, py = sx;
                fb.triangle(ex, ey, ex - sx * 18 * t + px * 8 * t, ey - cy * 18 * t + py * 8 * t, ex - sx * 18 * t - px * 8 * t, ey - cy * 18 * t - py * 8 * t, c);
            }
        }

        void arrow(Fb& fb, int i, int mx, int my, uint16_t c) {
            const double a = 9 * L.k, b = 12 * L.k;
            switch (i) {
                case 0: fb.triangle(mx - b / 2, my, mx + b / 2, my - a, mx + b / 2, my + a, c); break;          // CCW ◄
                case 2: fb.triangle(mx + b / 2, my, mx - b / 2, my - a, mx - b / 2, my + a, c); break;          // CW ►
                case 3: fb.triangle(mx, my + b / 2, mx - a, my - b / 2, mx + a, my - b / 2, c); break;          // DOWN ▼
                case 5: fb.triangle(mx, my - b / 2, mx - a, my + b / 2, mx + a, my + b / 2, c); break;          // UP ▲
                case 1: fb.fillRect(mx - (int)(7 * L.k), my - (int)(7 * L.k), (int)(14 * L.k), (int)(14 * L.k), c); break;   // STOP ■
                case 4: fb.ring(mx, my, 8 * L.k, 2.5 * L.k, c); fb.fillCircle(mx, my, (int)(2 * L.k), c); break;            // PARK ◎
            }
        }

        void mainPage(Fb& fb, const UiState& s) {
            const LcdFont &S = *L.S, &M = *L.M, &F = *L.L;
            const int R = L.r;
            // Brújula
            fb.fillCircle(L.cx, L.cy, R + 2, CARD);
            fb.ring(L.cx, L.cy, R + 2, 3 * L.k, LINE);
            for (int a = 0; a < 360; a += 10) {
                const double k = a * M_PI / 180.0, l = (a % 30 == 0 ? 12 : 6) * L.k;
                fb.line(L.cx + sin(k) * (R - l), L.cy - cos(k) * (R - l), L.cx + sin(k) * (R - 1), L.cy - cos(k) * (R - 1),
                        (a % 90 == 0 ? 3 : 1.5) * L.k, a % 30 == 0 ? FG : MUT);
            }
            const char* card[] = { "N", "E", "S", "W" };
            const int cr = R - (int)(28 * L.k);
            for (int i = 0; i < 4; i++) {
                const double k = i * M_PI / 2;
                fb.text(L.cx + (int)(sin(k) * cr), L.cy - (int)(cos(k) * cr) - M.height / 2, card[i], M, i == 0 ? RED : FG, CENTER);
            }
            for (int a = 30; a < 360; a += 30) {
                if (a % 90 == 0) continue;
                const double k = a * M_PI / 180.0;
                char t[12]; snprintf(t, sizeof t, "%d", a);
                fb.text(L.cx + (int)(sin(k) * L.tickLabelR), L.cy - (int)(cos(k) * L.tickLabelR) - S.height / 2, t, S, MUT, CENTER);
            }
            // Zona de solape (acimut mecánico > 360): arco naranja sobre el borde
            if (s.maxAz > 360) {
                for (double a = 0; a <= s.maxAz - 360; a += 1) {
                    const double k = a * M_PI / 180.0;
                    fb.fillCircle(L.cx + (int)(sin(k) * (R + 2)), L.cy - (int)(cos(k) * (R + 2)), (int)(2 * L.k), hex(0xd29922));
                }
            }
            if (!std::isnan(s.taz)) needle(fb, fmod(s.taz, 360.0), R - 16 * L.k, 3 * L.k, ACC, false);
            if (aiming(frameMs_)) needle(fb, aimAz_, R - 6 * L.k, 4 * L.k, GREEN, true);
            if (!std::isnan(s.az)) needle(fb, fmod(s.az, 360.0), R - 14 * L.k, 5 * L.k, RED, true);
            fb.fillCircle(L.cx, L.cy, (int)(7 * L.k), FG);
            fb.fillCircle(L.cx, L.cy, (int)(3 * L.k), BG);

            // Lecturas
            char t[40];
            if (std::isnan(s.az)) snprintf(t, sizeof t, "--"); else snprintf(t, sizeof t, "%.1f°", s.az);
            if (s.hasEl) {
                fb.text(L.azLx, L.azLy, "AZ", S, MUT);
                fb.text(L.azNx, L.azNy, t, F, FG, CENTER);
                fb.text(L.elLx, L.elLy, "EL", S, MUT);
                if (std::isnan(s.el)) snprintf(t, sizeof t, "--"); else snprintf(t, sizeof t, "%.1f°", s.el);
                fb.text(L.elNx, L.elNy, t, F, FG, CENTER);
                fb.fillRect(L.divider.x, L.divider.y, L.divider.w, L.divider.h, LINE);
            } else {
                fb.text(L.az1Lx, L.az1Ly, "AZ", S, MUT);
                fb.text(L.az1Nx, L.az1Ny, t, F, FG, CENTER);
            }

            // Estado: objetivo, movimiento, fallo
            std::string st;
            bool hint = false;
            if (aiming(frameMs_)) { snprintf(t, sizeof t, "Soltar: ir a %.0f°", aimAz_); st = t; }
            else {
                if (!std::isnan(s.taz)) {
                    snprintf(t, sizeof t, "→ %.1f°", s.taz); st = t;
                    if (s.hasEl && !std::isnan(s.tel)) { snprintf(t, sizeof t, " / %.1f°", s.tel); st += t; }
                    st += "  ";
                }
                if (s.moving) st += "moviendo  ";
                if (!std::isnan(s.maz) && s.maz >= 360) st += "solape  ";
                while (!st.empty() && st.back() == ' ') st.pop_back();
            }
            const Rect& sr = L.status;
            uint16_t col = FG;
            if (s.fault) {
                st = s.faultText + " (toca para borrar)";
                fb.fillRound(sr.x, sr.y, sr.w, sr.h, (int)(6 * L.k), hex(0x4a1a1a));
                col = RED;
            } else if (st.empty()) {
                hint = s.aprs.empty();
                st = hint ? "Mantén pulsada la brújula para apuntar" : ("APRS: " + s.aprs);
                col = hint ? MUT : FG;
            }
            std::vector<std::string> lines = wrap(S, st, sr.w - 8);
            if ((int)lines.size() > L.statusLines) lines.resize(L.statusLines);
            const int lh = S.height + 2, ty = sr.y + (sr.h - lh * (int)lines.size()) / 2;
            for (size_t i = 0; i < lines.size(); i++)
                fb.text(sr.x + sr.w / 2, ty + (int)i * lh, fit(S, lines[i], sr.w - 8).c_str(), S, col, CENTER);

            // Botones
            const char* lab[] = { "CCW", "STOP", "CW", "DOWN", "PARK", "UP" };
            for (int i = 0; i < 6; i++) {
                const Rect& b = L.btn[i];
                const bool dis = ((i == 0 || i == 2 || i == 3 || i == 5) && !s.canMove) || ((i == 3 || i == 5) && !s.hasEl);
                uint16_t bg = i == 1 ? STOPC : BTN;
                if (held_ == i && pressed_ && !dis) bg = i == 1 ? hex(0xff8080) : BTN_ON;
                fb.fillRound(b.x, b.y, b.w, b.h, (int)(10 * L.k), bg);
                const uint16_t fg = dis ? hex(0x4a5868) : WHITE;
                const int mx = b.x + b.w / 2, my = b.y + b.h / 2;
                if (L.stacked) {                                        // icono arriba, texto abajo
                    arrow(fb, i, mx, b.y + b.h / 3, fg);
                    fb.text(mx, b.y + b.h - S.height - (int)(6 * L.k), lab[i], S, fg, CENTER);
                } else {                                                // flecha y texto en la misma línea
                    const int tw = Fb::textWidth(M, lab[i]), aw = (i == 1 || i == 4) ? 0 : (int)(16 * L.k);
                    const int x0 = mx - (tw + aw + (aw ? 4 : 0)) / 2;
                    if (i == 2) {                                       // CW: texto y luego la flecha
                        fb.text(x0, my - M.height / 2, lab[i], M, fg);
                        arrow(fb, i, x0 + tw + 4 + aw / 2, my, fg);
                    } else {
                        if (aw) arrow(fb, i, x0 + aw / 2, my, fg);
                        fb.text(x0 + aw + (aw ? 4 : 0), my - M.height / 2, lab[i], M, fg);
                    }
                }
            }
        }

        void infoLine(Fb& fb, int& y, const char* k, const std::string& v) {
            const Rect& c = L.infoCard;
            fb.text(c.x + 8, y, k, *L.S, MUT);
            fb.text(c.x + L.infoValX, y, fit(*L.S, v.empty() ? "-" : v, c.w - L.infoValX - 8).c_str(), *L.S, FG);
            y += L.lineH;
        }

        void infoPage(Fb& fb, const UiState& s) {
            const LcdFont &S = *L.S, &M = *L.M;
            const Rect& ic = L.infoCard;
            fb.fillRound(ic.x, ic.y, ic.w, ic.h, (int)(10 * L.k), CARD);
            int y = ic.y + (L.land ? 10 : 6);
            char t[64];
            infoLine(fb, y, "Nombre", s.name);
            infoLine(fb, y, "Indicativo", s.call);
            infoLine(fb, y, "Placa", s.board);
            infoLine(fb, y, "Chip", s.chip);
            infoLine(fb, y, "Mando", s.drv);
            infoLine(fb, y, "WiFi", s.wifi);
            infoLine(fb, y, "IP", s.ip);
            std::string p;
            if (s.rotctld) { snprintf(t, sizeof t, "%d", s.rotctld); p = t; } else p = "apagado";
            if (L.land) {
                infoLine(fb, y, "rotctld", p);
                snprintf(t, sizeof t, "%d", s.apiPort);
                infoLine(fb, y, "JSON", s.apiPort ? t : "");
            } else {
                if (s.apiPort) { snprintf(t, sizeof t, " · JSON %d", s.apiPort); p += t; }
                infoLine(fb, y, "rotctld", p);
            }
            infoLine(fb, y, "Bluetooth", !s.ble ? "apagado" : (s.bleConn ? "BLE conectado" : "BLE esperando"));
            snprintf(t, sizeof t, "%luh %02lum · %s", (unsigned long)(s.upS / 3600), (unsigned long)(s.upS / 60 % 60), s.faultText.c_str());
            infoLine(fb, y, "Encendido", t);

            // Acerca de (como el tracker y la WebUI)
            const Rect& ac = L.aboutCard;
            fb.fillRound(ac.x, ac.y, ac.w, ac.h, (int)(10 * L.k), CARD);
            int ay = ac.y + 4;
            fb.text(ac.x + 8, ay, "Acerca de", M, MUT);
            ay += M.height + 4;
            const int aw = ac.w - 16, lh = S.height + (L.land ? 0 : 4);
            auto para = [&](const std::string& txt, uint16_t c) { for (const std::string& l : wrap(S, txt, aw)) { fb.text(ac.x + 8, ay, l.c_str(), S, c); ay += lh; } };
            para("APRS LoRa+ Rotor " + s.ver, FG);
            para("by Manuel EA7JRS", FG);
            para("© 2026 Manuel - EA7JRS", FG);
            para("github.com/EA7JRS/APRS-LoRa-Plus", ACC);
            para("GPL-3.0-or-later · DejaVu Sans", MUT);

            // Brillo
            fb.fillRound(L.brMinus.x, L.brMinus.y, L.brMinus.w, L.brMinus.h, (int)(10 * L.k), BTN);
            fb.fillRound(L.brPlus.x, L.brPlus.y, L.brPlus.w, L.brPlus.h, (int)(10 * L.k), BTN);
            fb.text(L.brMinus.x + L.brMinus.w / 2, L.brMinus.y + (L.brMinus.h - M.height) / 2, "−", M, WHITE, CENTER);
            fb.text(L.brPlus.x + L.brPlus.w / 2, L.brPlus.y + (L.brPlus.h - M.height) / 2, "+", M, WHITE, CENTER);
            snprintf(t, sizeof t, "Brillo %d%%", s.bright);
            fb.text(L.brTextX, L.brTextY, t, S, FG, CENTER);
        }
    };
}
