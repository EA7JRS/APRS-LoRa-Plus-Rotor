/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Pantallas de las placas con LCD táctil (320 x 480 en vertical): mando del rotor e información / acerca de.
// Sin Arduino: dibuja en un Lcd::Fb y traduce los toques en órdenes. lcd_hw.cpp lo une al hardware y al rotor;
// firmware/test_host/lcd_preview.cpp lo dibuja en el PC.
#include <stdio.h>
#include <cmath>
#include <string>
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

    class Ui {
    public:
        static constexpr int W = 320, H = 480;
        enum Page { MAIN, INFO };
        Page page = MAIN;

        // Colores (los de la WebUI)
        const uint16_t BG = hex(0x10151c), CARD = hex(0x19212b), FG = hex(0xe6edf3), MUT = hex(0x8b98a5), ACC = hex(0x4aa3ff);
        const uint16_t RED = hex(0xff5d5d), OK = hex(0x3fb950), HDR = hex(0xb21d1d), BTN = hex(0x263241), BTN_ON = hex(0x4aa3ff);
        const uint16_t LINE = hex(0x33475c), GREEN = hex(0x3fb950), WHITE = hex(0xffffff), STOPC = hex(0xd63a3a);

        // Geometría
        static constexpr int HDR_H = 48;
        static constexpr int CX = 160, CY = 154, R = 98;          // brújula
        static constexpr int BTN_Y = 362, BTN_H = 56, BTN_GAP = 6;

        // ------------------------------------------------------------------------------------------------- dibujo
        void draw(Fb& fb, const UiState& s) {
            fb.fill(BG);
            header(fb, s);
            if (page == MAIN) mainPage(fb, s); else infoPage(fb, s);
        }

        // ------------------------------------------------------------------------------------------------- toques
        // down: dedo en la pantalla (x, y válidos); up: se ha levantado. Llamar con cada lectura del táctil.
        UiCmd touch(bool down, int x, int y, uint32_t nowMs, const UiState& s) {
            UiCmd c;
            if (down && !pressed_) {                                   // empieza un toque
                pressed_ = true; startMs_ = nowMs; sx_ = x; sy_ = y; held_ = -1; aiming_ = false;
                if (inRect(x, y, 268, 4, 48, 40)) { page = page == MAIN ? INFO : MAIN; consumed_ = true; return c; }
                consumed_ = false;
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
                    if (s.fault && inRect(x, y, 0, 334, W, 24)) { c.cmd = Cmd::CLEAR_FAULT; return c; }
                } else {
                    if (inRect(x, y, 16, 426, 80, 48)) { c.cmd = Cmd::BRIGHT; c.value = s.bright - 10 < 10 ? 10 : s.bright - 10; return c; }
                    if (inRect(x, y, 224, 426, 80, 48)) { c.cmd = Cmd::BRIGHT; c.value = s.bright + 10 > 100 ? 100 : s.bright + 10; return c; }
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

    private:
        static constexpr uint32_t AIM_HOLD_MS = 500;
        bool     pressed_ = false, consumed_ = false, moveSent_ = false, aiming_ = false;
        int      held_ = -1, sx_ = 0, sy_ = 0;
        uint32_t startMs_ = 0;
        double   aimAz_ = 0;
        uint32_t frameMs_ = 0;

    public:
        void setFrameTime(uint32_t ms) { frameMs_ = ms; }

    private:
        static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
        static bool inCompass(int x, int y) { const int dx = x - CX, dy = y - CY; return dx * dx + dy * dy <= (R + 18) * (R + 18); }
        static double bearing(int x, int y) {
            double a = atan2((double)(x - CX), (double)(CY - y)) * 180.0 / M_PI;
            if (a < 0) a += 360;
            return round(a);
        }
        static void btnRect(int i, int& x, int& y, int& w, int& h) {
            const int bw = (W - 4 * BTN_GAP) / 3;
            x = BTN_GAP + (i % 3) * (bw + BTN_GAP);
            y = BTN_Y + (i / 3) * (BTN_H + BTN_GAP);
            w = bw; h = BTN_H;
        }
        static int buttonAt(int x, int y) {
            for (int i = 0; i < 6; i++) { int bx, by, bw, bh; btnRect(i, bx, by, bw, bh); if (inRect(x, y, bx, by, bw, bh)) return i; }
            return -1;
        }

        void header(Fb& fb, const UiState& s) {
            fb.fillRect(0, 0, W, HDR_H, HDR);
            fb.fillRect(0, HDR_H - 1, W, 1, hex(0x7a1414));
            for (int y = 0; y < LCD_LOGO_H; y++) for (int x = 0; x < LCD_LOGO_W; x++) fb.set(10 + x, 4 + y, LCD_LOGO[y * LCD_LOGO_W + x]);
            fb.text(56, 3, "APRS LoRa+", FontM, WHITE);
            std::string sub = "Rotor " + s.ver;
            fb.text(57, 27, sub.c_str(), FontS, hex(0xf0d0d0));
            // Botón de información (i) / volver (x)
            fb.ring(292, 24, 16, 2, WHITE);
            fb.text(292, 13, page == MAIN ? "i" : "×", FontM, WHITE, CENTER);
            // Enlaces: WiFi y BLE
            const uint16_t wc = s.ip.empty() ? hex(0x6d2a2a) : WHITE;
            fb.text(262, 6, "WiFi", FontS, wc, RIGHT);
            if (s.ble) fb.text(262, 26, s.bleConn ? "BLE ●" : "BLE", FontS, s.bleConn ? WHITE : hex(0xe6a0a0), RIGHT);
        }

        void needle(Fb& fb, double az, double len, double thick, uint16_t c, bool tip) {
            const double k = az * M_PI / 180.0, sx = sin(k), cy = -cos(k);
            const double ex = CX + sx * len, ey = CY + cy * len;
            fb.line(CX, CY, ex - sx * (tip ? 10 : 0), ey - cy * (tip ? 10 : 0), thick, c);
            if (tip) {
                const double px = -cy, py = sx;
                fb.triangle(ex, ey, ex - sx * 18 + px * 8, ey - cy * 18 + py * 8, ex - sx * 18 - px * 8, ey - cy * 18 - py * 8, c);
            }
        }

        void mainPage(Fb& fb, const UiState& s) {
            // Brújula
            fb.fillCircle(CX, CY, R + 2, CARD);
            fb.ring(CX, CY, R + 2, 3, LINE);
            for (int a = 0; a < 360; a += 10) {
                const double k = a * M_PI / 180.0, l = a % 30 == 0 ? 12 : 6;
                fb.line(CX + sin(k) * (R - l), CY - cos(k) * (R - l), CX + sin(k) * (R - 1), CY - cos(k) * (R - 1), a % 90 == 0 ? 3 : 1.5, a % 30 == 0 ? FG : MUT);
            }
            const char* card[] = { "N", "E", "S", "W" };
            for (int i = 0; i < 4; i++) {
                const double k = i * M_PI / 2;
                fb.text(CX + (int)(sin(k) * (R - 28)), CY - (int)(cos(k) * (R - 28)) - 11, card[i], FontM, i == 0 ? RED : FG, CENTER);
            }
            for (int a = 30; a < 360; a += 30) {
                if (a % 90 == 0) continue;
                const double k = a * M_PI / 180.0;
                char t[12]; snprintf(t, sizeof t, "%d", a);
                fb.text(CX + (int)(sin(k) * (R + 13)), CY - (int)(cos(k) * (R + 13)) - 8, t, FontS, MUT, CENTER);
            }
            // Zona de solape (acimut mecánico > 360): arco naranja sobre el borde
            if (s.maxAz > 360) {
                for (double a = 0; a <= s.maxAz - 360; a += 1) {
                    const double k = a * M_PI / 180.0;
                    fb.fillCircle(CX + (int)(sin(k) * (R + 2)), CY - (int)(cos(k) * (R + 2)), 2, hex(0xd29922));
                }
            }
            if (!std::isnan(s.taz)) needle(fb, fmod(s.taz, 360.0), R - 16, 3, ACC, false);
            if (aiming(frameMs_)) needle(fb, aimAz_, R - 6, 4, GREEN, true);
            if (!std::isnan(s.az)) needle(fb, fmod(s.az, 360.0), R - 14, 5, RED, true);
            fb.fillCircle(CX, CY, 7, FG);
            fb.fillCircle(CX, CY, 3, BG);

            // Lecturas
            char t[40];
            const int ry = 282;
            if (std::isnan(s.az)) snprintf(t, sizeof t, "--"); else snprintf(t, sizeof t, "%.1f°", s.az);
            if (s.hasEl) {
                fb.text(6, ry + 4, "AZ", FontS, MUT);
                fb.text(88, ry, t, FontL, FG, CENTER);
                fb.text(166, ry + 4, "EL", FontS, MUT);
                if (std::isnan(s.el)) snprintf(t, sizeof t, "--"); else snprintf(t, sizeof t, "%.1f°", s.el);
                fb.text(248, ry, t, FontL, FG, CENTER);
                fb.fillRect(160, ry + 2, 1, 40, LINE);
            } else {
                fb.text(60, ry + 4, "AZ", FontS, MUT);
                fb.text(165, ry, t, FontL, FG, CENTER);
            }

            // Estado: objetivo, movimiento, fallo
            std::string st;
            if (aiming(frameMs_)) { snprintf(t, sizeof t, "Soltar: ir a %.0f°", aimAz_); st = t; }
            else {
                if (!std::isnan(s.taz)) {
                    snprintf(t, sizeof t, "→ %.1f°", s.taz); st = t;
                    if (s.hasEl && !std::isnan(s.tel)) { snprintf(t, sizeof t, " / %.1f°", s.tel); st += t; }
                    st += "  ";
                }
                if (s.moving) st += "moviendo  ";
                if (!std::isnan(s.maz) && s.maz >= 360) st += "solape  ";
            }
            const int y = 334;
            if (s.fault) {
                std::string f = s.faultText + " (toca para borrar)";
                fb.fillRound(8, y - 2, W - 16, 24, 6, hex(0x4a1a1a));
                fb.text(W / 2, y, f.c_str(), FontS, RED, CENTER);
            } else {
                if (st.empty()) st = s.aprs.empty() ? "Mantén pulsada la brújula para apuntar" : ("APRS: " + s.aprs);
                fb.text(W / 2, y, st.c_str(), FontS, st[0] == 'M' ? MUT : FG, CENTER);
            }

            // Botones
            const char* lab[] = { "CCW", "STOP", "CW", "DOWN", "PARK", "UP" };
            for (int i = 0; i < 6; i++) {
                int bx, by, bw, bh; btnRect(i, bx, by, bw, bh);
                const bool dis = ((i == 0 || i == 2 || i == 3 || i == 5) && !s.canMove) || ((i == 3 || i == 5) && !s.hasEl);
                uint16_t bg = i == 1 ? STOPC : BTN;
                if (held_ == i && pressed_ && !dis) bg = i == 1 ? hex(0xff8080) : BTN_ON;
                fb.fillRound(bx, by, bw, bh, 10, bg);
                const uint16_t fg = dis ? hex(0x4a5868) : WHITE;
                const int mx = bx + bw / 2, my = by + bh / 2;
                switch (i) {                                                 // flechas dibujadas
                    case 0: fb.triangle(bx + 14, my, bx + 26, my - 9, bx + 26, my + 9, fg); break;
                    case 2: fb.triangle(bx + bw - 14, my, bx + bw - 26, my - 9, bx + bw - 26, my + 9, fg); break;
                    case 3: fb.triangle(bx + 16, my + 6, bx + 9, my - 5, bx + 23, my - 5, fg); break;
                    case 5: fb.triangle(bx + 20, my - 7, bx + 11, my + 6, bx + 29, my + 6, fg); break;
                }
                const int tx = i == 3 ? mx + 11 : ((i == 0 || i == 5) ? mx + 8 : (i == 2 ? mx - 8 : mx));
                fb.text(tx, my - 12, lab[i], FontM, fg, CENTER);
            }
        }

        void infoLine(Fb& fb, int& y, const char* k, const std::string& v) {
            fb.text(16, y, k, FontS, MUT);
            fb.text(118, y, v.empty() ? "-" : v.c_str(), FontS, FG);
            y += 21;
        }

        void infoPage(Fb& fb, const UiState& s) {
            fb.fillRound(8, 54, W - 16, 222, 10, CARD);
            int y = 60;
            char t[48];
            infoLine(fb, y, "Nombre", s.name);
            infoLine(fb, y, "Indicativo", s.call);
            infoLine(fb, y, "Placa", s.board);
            infoLine(fb, y, "Chip", s.chip);
            infoLine(fb, y, "Mando", s.drv);
            infoLine(fb, y, "WiFi", s.wifi);
            infoLine(fb, y, "IP", s.ip);
            std::string p;
            if (s.rotctld) { snprintf(t, sizeof t, "rotctld %d", s.rotctld); p = t; }
            if (s.apiPort) { snprintf(t, sizeof t, "%sJSON %d", p.empty() ? "" : " · ", s.apiPort); p += t; }
            infoLine(fb, y, "Puertos", p);
            infoLine(fb, y, "Bluetooth", !s.ble ? "apagado" : (s.bleConn ? "BLE conectado" : "BLE esperando"));
            snprintf(t, sizeof t, "%luh %02lum · %s", (unsigned long)(s.upS / 3600), (unsigned long)(s.upS / 60 % 60), s.faultText.c_str());
            infoLine(fb, y, "Encendido", t);

            // Acerca de (como el tracker y la WebUI)
            fb.fillRound(8, 282, W - 16, 138, 10, CARD);
            fb.text(16, 286, "Acerca de", FontM, MUT);
            fb.text(16, 312, ("APRS LoRa+ Rotor " + s.ver).c_str(), FontS, FG);
            fb.text(16, 331, "by Manuel EA7JRS", FontS, FG);
            fb.text(16, 350, "© 2026 Manuel - EA7JRS", FontS, FG);
            fb.text(16, 369, "github.com/EA7JRS/APRS-LoRa-Plus", FontS, ACC);
            fb.text(16, 392, "GPL-3.0-or-later · Fuente: DejaVu Sans", FontS, MUT);

            // Brillo
            fb.fillRound(16, 426, 80, 48, 10, BTN);
            fb.fillRound(224, 426, 80, 48, 10, BTN);
            fb.text(56, 437, "−", FontM, WHITE, CENTER);
            fb.text(264, 437, "+", FontM, WHITE, CENTER);
            snprintf(t, sizeof t, "Brillo %d%%", s.bright);
            fb.text(W / 2, 441, t, FontS, FG, CENTER);
        }
    };
}
