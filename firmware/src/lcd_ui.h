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
#include "lcd_fonts_smeter.h"
#include "lcd_logo.h"
#include "lcd_splash.h"

namespace Lcd {

    // Paletas: la de la app Android (Theme.kt, de serie; la misma que APRS LoRa+ Display) y la de antes (cabecera roja)
    struct Pal {
        uint32_t bg, card, cardB, fg, mut, acc, red, green, yel, line;
        uint32_t hdr, hdrLine, title, sub, linkOn, linkOff;              // cabecera
        uint32_t btn, btnB, btnText, btnPr, btnPrText, sel, selText;     // botones (normal, pulsado, elegido)
        uint32_t stop, stopPr, dis, faultBg, trackBg, pillMode, pillRx, overlap;
        uint32_t faint, off;                                             // escala y lectura apagada del S-meter
    };
    // Colores de los segmentos del S-meter, los de la app Android (Widgets.kt S_COLORS; escala IARU como en las emisoras
    // Yaesu / Icom actuales): S1-S5 verde claro a azul verdoso, S6-S8 amarillo, S9 naranja, +10 ... +60 dB rojo
    static const uint32_t S_COLORS[15] = {
        0x4FC3E0, 0x52D4C0, 0x5BDFA0, 0x72E688, 0x96EA74,
        0xD4E650, 0xF2DE3E, 0xFFCF33,
        0xFF9A2E,
        0xFF5A3C, 0xFF4636, 0xFF3A33, 0xFF3030, 0xFF2A2A, 0xFF2424 };
    // c sobre bg con opacidad a (como Color.copy(alpha) de Compose)
    inline uint32_t mix(uint32_t bg, uint32_t c, double a) {
        auto ch = [&](int sh) { const int b = (bg >> sh) & 255, f = (c >> sh) & 255; return (uint32_t)(b + (f - b) * a + 0.5) << sh; };
        return ch(16) | ch(8) | ch(0);
    }
    enum Theme { THEME_APP = 0, THEME_ROTOR = 1 };
    static const Pal PAL_APP = {    // fondo gris azulado, verde y cian del logo; ámbar para lo elegido
        0x131A22, 0x18222D, 0x263443, 0xE8EEF3, 0x9FB0BF, 0x28B5E1, 0xFF6B6B, 0x3CE68A, 0xFFB347, 0x263443,
        0x131A22, 0x263443, 0xE8EEF3, 0x28B5E1, 0x3CE68A, 0xFFB347,
        0x1E2A36, 0x2E3E4F, 0xE8EEF3, 0x28B5E1, 0x131A22, 0xFFB347, 0x131A22,
        0xD64545, 0xFF6B6B, 0x4A5868, 0x3A1E24, 0x15303D, 0x1E3A4A, 0x1C4030, 0xFFB347,
        0x6F8293, 0x4A5D70 };
    static const Pal PAL_ROTOR = {  // la de la WebUI: fondo oscuro, azul, cabecera roja
        0x10151c, 0x19212b, 0x19212b, 0xe6edf3, 0x8b98a5, 0x4aa3ff, 0xff5d5d, 0x3fb950, 0xffd23f, 0x33475c,
        0xb21d1d, 0x7a1414, 0xffffff, 0xf0d0d0, 0xffffff, 0xd88a8a,
        0x263241, 0x263241, 0xffffff, 0x4aa3ff, 0xffffff, 0x2f6fb0, 0xffffff,
        0xd63a3a, 0xff8080, 0x4a5868, 0x4a1a1a, 0x14324a, 0x26445f, 0x1f4d2b, 0xd29922,
        0x66727f, 0x45505c };

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
        std::string wind;                           // tiempo de internet de la app: "12/20 m/s" (medio / racha; vacío = sin datos)
        // Equipo
        std::string ver, name, call, board, chip, drv, ip, wifi;
        int         rotctld = 0, apiPort = 0;
        bool        ble = false, bleConn = false;
        uint32_t    upS = 0;
        int         bright = 80;
        // CAT (emisora)
        bool        catOn = false;                  // CAT configurado
        bool        catLink = false;                // la emisora contesta
        std::string rig;                            // "IC-9700 · CI-V A2 · 19200"
        uint64_t    freq = 0, txFreq = 0;           // Hz (txFreq > 0: split / satélite)
        std::string mode, txMode;                   // "FM", "USB"...
        char        vfo = 'A';
        bool        ptt = false, allowPtt = false;
        bool        rxOnly = false;                 // receptor (SDR): sin PTT ni A/B
        int         smeter = -1;                    // 0-15 (S0..S9 = 0..9, S9+10..+60 = 10..15); -1 sin dato
        int         tone = 0;                       // CTCSS de emisión, décimas de Hz (0 = no)
        uint32_t    step = 1000;                    // paso de sintonía (Hz)
        bool        tracking = false;               // seguimiento de satélite / Doppler
        std::string trackName;                      // "ISS"
        int32_t     doppler = 0;                    // Hz aplicados a la bajada
    };

    enum class Cmd { NONE, MOVE, STOP, PARK, GOTO, CLEAR_FAULT, BRIGHT, CAT };

    // Órdenes a la emisora desde la pantalla
    enum class CatOp { NONE, TUNE, STEP, MODE, VFO, TRACK, PTT_ON, PTT_OFF };

    static const char* const CAT_MODES[6] = { "LSB", "USB", "CW", "FM", "AM", "DIG" };
    static const uint32_t CAT_STEPS[7] = { 10, 100, 1000, 5000, 12500, 25000, 100000 };

    struct UiCmd {
        Cmd    cmd = Cmd::NONE;
        int    dir = 0;          // MOVE: direcciones RC::MOVE_* (2 UP, 4 DOWN, 8 CCW, 16 CW)
        double az = NAN;         // GOTO
        int    value = 0;        // BRIGHT: nuevo brillo (%); CAT TUNE: +1 / -1 pasos; STEP: nuevo paso (Hz); TRACK: 0/1
        CatOp  cat = CatOp::NONE;
        std::string text;        // CAT MODE: "USB"...
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
        double        dp = 1.2;                      // puntos por dp de la app (S-meter con las medidas de Widgets.kt)
        const LcdFont* smL = &FontSmL1;              // escala del S-meter (10 sp) y lectura (18 sp)
        const LcdFont* smR = &FontSmR1;
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
        // Botón CAT de la cabecera y página CAT
        Rect          catBtn = { 212, 10, 50, 28 };
        int           linkMode = 0;                  // 0: estado WiFi/BLE junto a la versión; 1: a la izquierda del botón CAT
        Rect          freqCard = { 8, 54, 304, 106 }, smeter = { 16, 163, 288, 37 }, trackCard = { 8, 202, 304, 50 };
        Rect          tuneDown = { 8, 260, 92, 52 }, stepBtn = { 106, 260, 108, 52 }, tuneUp = { 220, 260, 92, 52 };
        Rect          modeBtn[6], trackBtn = { 8, 414, 98, 60 }, vfoBtn = { 112, 414, 96, 60 }, pttBtn = { 214, 414, 98, 60 };

        static Layout make(int w, int h) {
            Layout l;
            l.W = w; l.H = h;
            if (w <= h) {                            // vertical 320 x 480: rejilla de 3 x 2 botones abajo
                const int gap = 6, bw = (w - 4 * gap) / 3, bh = 56, y0 = 362;
                for (int i = 0; i < 6; i++) l.btn[i] = { gap + (i % 3) * (bw + gap), y0 + (i / 3) * (bh + gap), bw, bh };
                const int mw = (w - 16 - 5 * 4) / 6;
                for (int i = 0; i < 6; i++) l.modeBtn[i] = { 8 + i * (mw + 4), 320, mw, 44 };
                // fila de modos (320-364); fila de TX / split (370-406); botones abajo (414-474)
                return l;
            }
            if (w >= 800) return big(w, h);
            // Apaisada 600 x 450: brújula a la izquierda, lecturas a la derecha, fila de 6 botones abajo
            l.land = true;
            l.S = &FontS2; l.M = &FontM2; l.L = &FontL2; l.k = 1.4; l.dp = 1.4; l.smL = &FontSmL2; l.smR = &FontSmR2;
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
            l.catBtn = { 452, 12, 70, 34 }; l.linkMode = 1; l.linkX = 440;
            l.freqCard = { 8, 62, 584, 132 }; l.smeter = { 16, 196, 568, 41 }; l.trackCard = { 8, 238, 584, 60 };
            const int bw2 = (288 - 2 * 6) / 3, bh2 = 64, ya = 308, yb = 380;
            l.tuneDown = { 8, ya, bw2, bh2 }; l.stepBtn = { 8 + bw2 + 6, ya, bw2, bh2 }; l.tuneUp = { 8 + 2 * (bw2 + 6), ya, bw2, bh2 };
            l.trackBtn = { 8, yb, bw2, bh2 }; l.vfoBtn = { 8 + bw2 + 6, yb, bw2, bh2 }; l.pttBtn = { 8 + 2 * (bw2 + 6), yb, bw2, bh2 };
            for (int i = 0; i < 6; i++) l.modeBtn[i] = { 304 + (i % 3) * (bw2 + 6), (i < 3 ? ya : yb), bw2, bh2 };
            return l;
        }

        // Pantallas grandes apaisadas (1024 x 600 de la ESP32-S3-Touch-LCD-7B): la disposición de 600 x 450 a escala,
        // con letras mayores; la brújula y los grosores con la escala vertical para que sigan redondos.
        static Layout big(int w, int h) {
            Layout l = make(600, 450);
            const double sx = w / 600.0, sy = h / 450.0;
            auto X = [&](int& v) { v = (int)lround(v * sx); };
            auto Y = [&](int& v) { v = (int)lround(v * sy); };
            auto R = [&](Rect& r) { const int x2 = (int)lround((r.x + r.w) * sx), y2 = (int)lround((r.y + r.h) * sy); X(r.x); Y(r.y); r.w = x2 - r.x; r.h = y2 - r.y; };
            l.W = w; l.H = h;
            l.S = &FontS3; l.M = &FontM3; l.L = &FontL3; l.k = 1.4 * sy; l.dp = 1.4 * sy; l.smL = &FontSmL3; l.smR = &FontSmR3;
            Y(l.hdrH); X(l.titleX); Y(l.titleY); Y(l.subY); X(l.logoX); l.logoY = (l.hdrH - LCD_LOGO_H) / 2;
            X(l.infoX); Y(l.infoY); Y(l.infoR); X(l.linkX); Y(l.link1Y); Y(l.link2Y);
            X(l.cx); Y(l.cy); Y(l.r); Y(l.tickLabelR);
            X(l.azLx); Y(l.azLy); X(l.azNx); Y(l.azNy); X(l.elLx); Y(l.elLy); X(l.elNx); Y(l.elNy);
            X(l.az1Lx); Y(l.az1Ly); X(l.az1Nx); Y(l.az1Ny);
            R(l.divider); l.divider.h = 1; R(l.status);
            for (Rect& b : l.btn) R(b);
            R(l.infoCard); R(l.aboutCard); X(l.infoValX); Y(l.lineH);
            R(l.brMinus); R(l.brPlus); X(l.brTextX); Y(l.brTextY);
            R(l.catBtn);
            R(l.freqCard); R(l.smeter); R(l.trackCard); R(l.tuneDown); R(l.stepBtn); R(l.tuneUp);
            for (Rect& b : l.modeBtn) R(b);
            R(l.trackBtn); R(l.vfoBtn); R(l.pttBtn);
            return l;
        }
    };

    class Ui {
    public:
        explicit Ui(int w = 320, int h = 480, int theme = THEME_APP) : L(Layout::make(w, h)) { setTheme(theme); }
        const Layout L;
        enum Page { MAIN, INFO, CAT };
        Page page = MAIN;

        // Colores de la paleta elegida (lcd.theme)
        int theme = -1;
        uint16_t BG, CARD, CARDB, FG, MUT, ACC, RED, GREEN, YEL, LINE, HDR, HDRL, TITLE, SUB, LINK_ON, LINK_OFF;
        uint16_t BTN, BTNB, BTNT, BTN_ON, BTN_ONT, SEL, SELT, STOPC, STOP_PR, DIS, FAULT_BG, TRACK_BG, PILL_MODE, PILL_RX, OVERLAP;
        const uint16_t WHITE = hex(0xffffff);
        uint32_t bg888 = 0, fg888 = 0, faint888 = 0, off888 = 0;
        void setTheme(int t) {
            if (t != THEME_ROTOR) t = THEME_APP;
            if (t == theme) return;
            theme = t;
            const Pal& p = t == THEME_ROTOR ? PAL_ROTOR : PAL_APP;
            bg888 = p.bg; fg888 = p.fg; faint888 = p.faint; off888 = p.off;
            BG = hex(p.bg); CARD = hex(p.card); CARDB = hex(p.cardB); FG = hex(p.fg); MUT = hex(p.mut); ACC = hex(p.acc);
            RED = hex(p.red); GREEN = hex(p.green); YEL = hex(p.yel); LINE = hex(p.line);
            HDR = hex(p.hdr); HDRL = hex(p.hdrLine); TITLE = hex(p.title); SUB = hex(p.sub); LINK_ON = hex(p.linkOn); LINK_OFF = hex(p.linkOff);
            BTN = hex(p.btn); BTNB = hex(p.btnB); BTNT = hex(p.btnText); BTN_ON = hex(p.btnPr); BTN_ONT = hex(p.btnPrText);
            SEL = hex(p.sel); SELT = hex(p.selText); STOPC = hex(p.stop); STOP_PR = hex(p.stopPr); DIS = hex(p.dis);
            FAULT_BG = hex(p.faultBg); TRACK_BG = hex(p.trackBg); PILL_MODE = hex(p.pillMode); PILL_RX = hex(p.pillRx); OVERLAP = hex(p.overlap);
        }
        // Tarjeta o botón con borde (si la paleta lo lleva)
        void box(Fb& fb, int x, int y, int w, int h, int r, uint16_t fill, uint16_t border) {
            if (border == fill) { fb.fillRound(x, y, w, h, r, fill); return; }
            fb.fillRound(x, y, w, h, r, border);
            fb.fillRound(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, fill);
        }
        // S-meter como SMeterBar de la app: 9 segmentos S y 6 sobre S9 con los colores IARU, los apagados con el 15 % de su
        // color, marca entre S9 y +10, escala 1 3 5 7 9 / +20 +40 +60 debajo y la lectura («S7», «S9+20») a la derecha.
        // v: 0-9 = S0-S9, 10-15 = S9+10 ... +60, < 0 sin dato. Medidas en dp de la app por L.dp.
        void sMeter(Fb& fb, const Rect& sm, int v) {
            auto D = [&](double x) { return (int)std::lround(x * L.dp); };
            const int lit = v < 0 ? 0 : (v > 15 ? 15 : v);
            char lab[12];
            if (v < 0) snprintf(lab, sizeof lab, "S—");
            else if (v <= 9) snprintf(lab, sizeof lab, "S%d", v);
            else snprintf(lab, sizeof lab, "S9+%d", (lit - 9) * 10);
            const LcdFont& fl = *L.smL; const LcdFont& fr = *L.smR;
            const int readW = Fb::textWidth(fr, lab);
            const int colW = sm.w - D(8) - readW;                    // Row con spacedBy(8.dp): barra | lectura
            const int colH = D(16) + D(1) + fl.height, rowH = colH > fr.height ? colH : fr.height;
            const int top = sm.y + (sm.h - rowH) / 2, colY = top + (rowH - colH) / 2;
            const double wsum = colW - D(8), leftW = wsum * 9 / 15, rightW = wsum * 6 / 15;   // 3 + 2 + 3 dp de la marca
            const int bottom = colY + D(16), gap = D(2), r = D(2);
            auto seg = [&](int i, double x0, double w, int n, int k) {
                const double sw = (w - gap * (n - 1)) / n;
                const int x = (int)std::lround(x0 + k * (sw + gap)), x2 = (int)std::lround(x0 + k * (sw + gap) + sw);
                const int hgt = i <= 9 ? D(10) : D(13);
                const uint32_t c = S_COLORS[i - 1];
                fb.fillRound(x, bottom - hgt, x2 - x, hgt, r, hex(i <= lit ? c : mix(bg888, c, 0.15)));
            };
            const double x0 = sm.x, xr = sm.x + leftW + D(8);
            for (int i = 1; i <= 9; i++) seg(i, x0, leftW, 9, i - 1);
            fb.fillRound((int)std::lround(x0 + leftW) + D(3), bottom - D(16), D(2), D(16), D(1), hex(mix(bg888, fg888, 0.8)));
            for (int i = 10; i <= 15; i++) seg(i, xr, rightW, 6, i - 10);
            const int ly = bottom + D(1);
            static const char* const LEFT_T[5] = { "1", "3", "5", "7", "9" };
            for (int j = 0; j < 5; j++)
                fb.text((int)std::lround(x0 + leftW * (2 * j) / 9), ly, LEFT_T[j], fl, hex(j == 4 ? S_COLORS[8] : faint888));
            static const char* const RIGHT_T[3] = { "+20", "+40", "+60" };
            for (int j = 0; j < 3; j++)
                fb.text((int)std::lround(xr + rightW * (j + 1) / 3), ly, RIGHT_T[j], fl, hex(faint888), RIGHT);
            fb.text(sm.x + sm.w, top + (rowH - fr.height) / 2, lab, fr, hex(lit == 0 ? off888 : S_COLORS[lit - 1]), RIGHT);
        }
        void card(Fb& fb, const Rect& c, uint16_t fill) { box(fb, c.x, c.y, c.w, c.h, (int)(10 * L.k), fill, CARDB); }

        // ------------------------------------------------------------------------------------------------- dibujo
        // Pantalla de arranque: la de bienvenida de la app (logo sobre #131A22) y debajo "Rotor <versión>".
        // En las pantallas grandes se amplía (bilineal) hasta llenar el alto.
        void splash(Fb& fb, const char* ver) {
            const uint16_t bg = hex(LCD_SPLASH_BG);
            fb.fill(bg);
            const int th = L.S->height + 6;
            double k = (double)(L.H - th - 12) / LCD_SPLASH_H;
            if (k < 1) k = 1;
            const int sw = (int)(LCD_SPLASH_W * k), sh = (int)(LCD_SPLASH_H * k);
            const int x0 = (L.W - sw) / 2, y0 = (L.H - sh - th) / 2;
            for (int y = 0; y < sh; y++) {
                if (k == 1) {
                    const uint8_t* row = LCD_SPLASH + (size_t)y * LCD_SPLASH_W;
                    for (int x = 0; x < sw; x++) fb.set(x0 + x, y0 + y, LCD_SPLASH_PAL[row[x]]);
                    continue;
                }
                const double fy = (y + 0.5) / k - 0.5;
                const int iy = fy < 0 ? 0 : (int)fy, iy1 = iy + 1 < LCD_SPLASH_H ? iy + 1 : iy;
                const int wy = (int)((fy - iy) * 15 + 0.5);
                for (int x = 0; x < sw; x++) {
                    const double fx = (x + 0.5) / k - 0.5;
                    const int ix = fx < 0 ? 0 : (int)fx, ix1 = ix + 1 < LCD_SPLASH_W ? ix + 1 : ix;
                    const int wx = (int)((fx - ix) * 15 + 0.5);
                    const uint8_t* r0 = LCD_SPLASH + (size_t)iy * LCD_SPLASH_W;
                    const uint8_t* r1 = LCD_SPLASH + (size_t)iy1 * LCD_SPLASH_W;
                    const uint16_t a = blend(LCD_SPLASH_PAL[r0[ix]], LCD_SPLASH_PAL[r0[ix1]], (uint8_t)wx);
                    const uint16_t b = blend(LCD_SPLASH_PAL[r1[ix]], LCD_SPLASH_PAL[r1[ix1]], (uint8_t)wx);
                    fb.set(x0 + x, y0 + y, blend(a, b, (uint8_t)wy));
                }
            }
            const std::string sub = std::string("Rotor ") + (ver ? ver : "");
            fb.text(L.W / 2, y0 + sh + 4, sub.c_str(), *L.S, hex(0x28b5e1), CENTER);
        }

        void draw(Fb& fb, const UiState& s) {
            fb.fill(BG);
            header(fb, s);
            if (page == CAT && !s.catOn) page = MAIN;
            if (page == MAIN) mainPage(fb, s); else if (page == INFO) infoPage(fb, s); else catPage(fb, s);
        }

        // ------------------------------------------------------------------------------------------------- toques
        // down: dedo en la pantalla (x, y válidos); al levantarlo, down = false. Llamar con cada lectura del táctil.
        UiCmd touch(bool down, int x, int y, uint32_t nowMs, const UiState& s) {
            UiCmd c;
            if (down && !pressed_) {                                   // empieza un toque
                pressed_ = true; startMs_ = nowMs; held_ = -1; aiming_ = false; moveSent_ = false;
                const int dx = x - L.infoX, dy = y - L.infoY, rr = L.infoR + 14;
                if (dx * dx + dy * dy <= rr * rr) { page = page == INFO ? MAIN : INFO; return c; }
                const Rect cb = { L.catBtn.x - 6, 0, L.catBtn.w + 12, L.hdrH };
                if (s.catOn && cb.in(x, y)) { page = page == CAT ? MAIN : CAT; return c; }
                if (page == CAT) return catTouch(x, y, s);
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
                if (pttSent_) { pttSent_ = false; c.cmd = Cmd::CAT; c.cat = CatOp::PTT_OFF; held_ = -1; return c; }
                if (moveSent_) { moveSent_ = false; c.cmd = Cmd::STOP; }
                else if (aiming_ && nowMs - startMs_ >= AIM_HOLD_MS) { c.cmd = Cmd::GOTO; c.az = aimAz_; }
                aiming_ = false; held_ = -1;
            }
            return c;
        }

        static std::string fmtFreqPublic(uint64_t hz) { return fmtFreq(hz); }
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
        bool     pressed_ = false, moveSent_ = false, aiming_ = false, pttSent_ = false;
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
            fb.fillRect(0, L.hdrH - 1, L.W, 1, HDRL);
            for (int y = 0; y < LCD_LOGO_H; y++) for (int x = 0; x < LCD_LOGO_W; x++) fb.set(L.logoX + x, L.logoY + y, (theme == THEME_ROTOR ? LCD_LOGO : LCD_LOGO_APP)[y * LCD_LOGO_W + x]);
            fb.text(L.titleX, L.titleY, "APRS LoRa+", *L.M, TITLE);
            const std::string sub = "Rotor " + s.ver;
            fb.text(L.titleX + 1, L.subY, sub.c_str(), *L.S, SUB);
            // Botón de información (i) / volver (×)
            fb.ring(L.infoX, L.infoY, L.infoR, 2 * L.k, TITLE);
            fb.text(L.infoX, L.infoY - L.M->height / 2 + 1, page == INFO ? "×" : "i", *L.M, TITLE, CENTER);
            // Botón CAT (si hay emisora configurada)
            if (s.catOn) {
                const Rect& b = L.catBtn;
                if (page == CAT) fb.fillRound(b.x, b.y, b.w, b.h, b.h / 2, TITLE);
                else { fb.fillRound(b.x, b.y, b.w, b.h, b.h / 2, TITLE); fb.fillRound(b.x + 2, b.y + 2, b.w - 4, b.h - 4, b.h / 2 - 2, HDR); }
                fb.text(b.x + b.w / 2, b.y + (b.h - L.S->height) / 2, "CAT", *L.S, page == CAT ? HDR : TITLE, CENTER);
                if (s.catLink) fb.fillCircle(b.x + b.w - 4, b.y + 4, (int)(4 * L.k), s.ptt ? YEL : GREEN);
            }
            // Enlaces: WiFi y BLE
            if (L.linkMode == 1) {
                fb.text(L.linkX, L.link1Y, "WiFi", *L.S, s.ip.empty() ? LINK_OFF : LINK_ON, RIGHT);
                if (s.ble) fb.text(L.linkX, L.link2Y, s.bleConn ? "BLE ●" : "BLE", *L.S, s.bleConn ? LINK_ON : LINK_OFF, RIGHT);
            } else {
                int x = L.titleX + 1 + Fb::textWidth(*L.S, sub.c_str()) + 8;      // blanco = conectado
                x = fb.text(x, L.subY, "WiFi", *L.S, s.ip.empty() ? LINK_OFF : LINK_ON) + 5;
                if (s.ble) fb.text(x, L.subY, "BLE", *L.S, s.bleConn ? LINK_ON : LINK_OFF);
            }
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
                    fb.fillCircle(L.cx + (int)(sin(k) * (R + 2)), L.cy - (int)(cos(k) * (R + 2)), (int)(2 * L.k), OVERLAP);
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
            if (s.fault == 6) {                                        // RC::F_WIND
                st = "Aparcado por viento" + (s.wind.empty() ? std::string() : " (" + s.wind + ")");
                if (L.statusLines > 1) st += " · toca para liberar";
                fb.fillRound(sr.x, sr.y, sr.w, sr.h, (int)(6 * L.k), FAULT_BG);
                col = RED;
            } else if (s.fault) {
                st = s.faultText + " (toca para borrar)";
                fb.fillRound(sr.x, sr.y, sr.w, sr.h, (int)(6 * L.k), FAULT_BG);
                col = RED;
            } else if (st.empty() && s.aprs.empty() && !s.wind.empty()) {
                st = "Viento " + s.wind;
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
                const bool pr = held_ == i && pressed_ && !dis;
                const uint16_t bg = i == 1 ? (pr ? STOP_PR : STOPC) : (pr ? BTN_ON : BTN);
                box(fb, b.x, b.y, b.w, b.h, (int)(10 * L.k), bg, i == 1 || pr ? bg : BTNB);
                const uint16_t fg = dis ? DIS : (i == 1 ? WHITE : (pr ? BTN_ONT : BTNT));
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


        // ------------------------------------------------------------------------------------------------- CAT
        static std::string fmtFreq(uint64_t hz) {               // 145825000 -> "145.825.000"
            char t[24];
            snprintf(t, sizeof t, "%llu", (unsigned long long)hz);
            std::string d = t, o;
            const int n = (int)d.size();
            for (int i = 0; i < n; i++) { o += d[i]; const int r = n - 1 - i; if (r > 0 && r % 3 == 0) o += '.'; }
            return o;
        }
        static std::string dispMode(const std::string& m) {
            if (m == "PKTUSB") return "DIG-U";
            if (m == "PKTLSB") return "DIG-L";
            if (m == "PKTFM") return "DIG-FM";
            return m;
        }
        static std::string fmtStep(uint32_t hz) {
            char t[16];
            if (hz >= 1000) { if (hz % 1000) snprintf(t, sizeof t, "%.1f kHz", hz / 1000.0); else snprintf(t, sizeof t, "%u kHz", (unsigned)(hz / 1000)); }
            else snprintf(t, sizeof t, "%u Hz", (unsigned)hz);
            return t;
        }

        UiCmd catTouch(int x, int y, const UiState& s) {
            UiCmd c;
            c.cmd = Cmd::CAT;
            if (L.tuneDown.in(x, y)) { held_ = 100; c.cat = CatOp::TUNE; c.value = -1; return c; }
            if (L.tuneUp.in(x, y))   { held_ = 101; c.cat = CatOp::TUNE; c.value = 1; return c; }
            if (L.stepBtn.in(x, y)) {
                held_ = 102;
                int i = 0;
                while (i < 7 && CAT_STEPS[i] != s.step) i++;
                c.cat = CatOp::STEP; c.value = (int)CAT_STEPS[(i + 1) % 7];
                return c;
            }
            for (int i = 0; i < 6; i++) if (L.modeBtn[i].in(x, y)) { held_ = 110 + i; c.cat = CatOp::MODE; c.text = CAT_MODES[i]; return c; }
            if (L.trackBtn.in(x, y)) { held_ = 103; c.cat = CatOp::TRACK; c.value = s.tracking ? 0 : 1; return c; }
            if (L.vfoBtn.in(x, y) && !s.rxOnly) { held_ = 104; c.cat = CatOp::VFO; return c; }
            if (L.pttBtn.in(x, y) && s.allowPtt && s.catLink) { held_ = 105; pttSent_ = true; c.cat = CatOp::PTT_ON; return c; }
            c.cmd = Cmd::NONE;
            return c;
        }

        void catButton(Fb& fb, const Rect& b, int id, const char* label, bool on, bool dis, uint16_t onColor) {
            const bool pr = held_ == id && pressed_ && !dis && !on;
            const uint16_t bg = on ? onColor : (pr ? BTN_ON : BTN);
            const uint16_t tc = dis ? DIS : (on ? (onColor == SEL ? SELT : WHITE) : (pr ? BTN_ONT : BTNT));
            box(fb, b.x, b.y, b.w, b.h, (int)(10 * L.k), bg, on || pr ? bg : BTNB);
            const bool big = b.h >= 50 && Fb::textWidth(*L.M, label) <= b.w - 8;
            const LcdFont& f = big ? *L.M : *L.S;
            std::vector<std::string> ls = wrap(f, label, b.w - 8);
            const int lh = f.height, ty = b.y + (b.h - lh * (int)ls.size()) / 2;
            for (size_t i = 0; i < ls.size(); i++) fb.text(b.x + b.w / 2, ty + (int)i * lh, ls[i].c_str(), f, tc, CENTER);
        }

        void catPage(Fb& fb, const UiState& s) {
            const LcdFont &S = *L.S, &F = *L.L;
            // Frecuencia
            const Rect& fc = L.freqCard;
            card(fb, fc, CARD);
            if (s.ptt) fb.fillRect(fc.x + 6, fc.y + fc.h - (int)(4 * L.k), fc.w - 12, (int)(3 * L.k), RED);
            fb.fillCircle(fc.x + fc.w - (int)(14 * L.k), fc.y + (int)(14 * L.k), (int)(5 * L.k), s.catLink ? GREEN : RED);
            // Tono CTCSS a la derecha de la línea de la emisora (en vertical no cabe con la subida)
            int rigW = fc.w - (int)(40 * L.k);
            if (s.tone > 0) {
                char tb[20]; snprintf(tb, sizeof tb, "CTCSS %d.%d", s.tone / 10, s.tone % 10);
                const int tx = fc.x + fc.w - (int)(26 * L.k);
                fb.text(tx, fc.y + 6, tb, S, YEL, RIGHT);
                rigW -= Fb::textWidth(S, tb) + 8;
            }
            fb.text(fc.x + 10, fc.y + 6, fit(S, s.rig.empty() ? "Emisora" : s.rig, rigW).c_str(), S, MUT);
            std::string fq = s.catLink && s.freq ? fmtFreq(s.freq) : "---.---.---";
            const int mhzW = Fb::textWidth(S, "MHz") + 6, fw = Fb::textWidth(F, fq.c_str());
            const int fx = fc.x + (fc.w - fw - mhzW) / 2, fy = fc.y + S.height + 8;
            fb.text(fx, fy, fq.c_str(), F, s.catLink ? FG : MUT);
            fb.text(fx + fw + 6, fy + F.ascent - S.ascent, "MHz", S, MUT);
            // Modo, VFO, RX/TX
            int px = fc.x + 10;
            const int py = fy + F.height + (int)(2 * L.k);
            auto pill = [&](const std::string& t, uint16_t bg, uint16_t fg) {
                const int w = Fb::textWidth(S, t.c_str()) + (int)(16 * L.k), h = S.height + 4;
                fb.fillRound(px, py, w, h, h / 2, bg);
                fb.text(px + w / 2, py + 2, t.c_str(), S, fg, CENTER);
                px += w + 6;
            };
            pill(s.mode.empty() ? "--" : dispMode(s.mode), PILL_MODE, FG);
            if (!s.rxOnly) pill(std::string("VFO ") + s.vfo, PILL_MODE, FG);
            pill(s.ptt ? "TX" : "RX", s.ptt ? RED : PILL_RX, s.ptt ? WHITE : FG);
            if (s.txFreq) {
                const std::string t = "↑ " + fmtFreq(s.txFreq) + (s.txMode.empty() ? "" : " " + s.txMode);
                fb.text(fc.x + fc.w - 10, py + 2, fit(S, t, fc.x + fc.w - 10 - px).c_str(), S, s.ptt ? RED : FG, RIGHT);
            }
            // S-meter: el de la app (Widgets.kt SMeterBar)
            sMeter(fb, L.smeter, s.smeter);
            char t[48];
            // Seguimiento (satélite / Doppler)
            const Rect& tc = L.trackCard;
            card(fb, tc, s.tracking ? TRACK_BG : CARD);
            if (s.tracking) {
                snprintf(t, sizeof t, "Siguiendo %s", s.trackName.empty() ? "satélite" : s.trackName.c_str());
                fb.text(tc.x + 10, tc.y + 4, t, S, ACC);
                snprintf(t, sizeof t, "Doppler %+.1f kHz", s.doppler / 1000.0);
                fb.text(tc.x + 10, tc.y + 4 + S.height, t, S, FG);
            } else {
                fb.text(tc.x + 10, tc.y + 4, "Seguimiento apagado", S, MUT);
                fb.text(tc.x + 10, tc.y + 4 + S.height, "SAT del tracker o rigctld (GPredict)", S, MUT);
            }
            // Sintonía
            catButton(fb, L.tuneDown, 100, "−", false, !s.catLink, BTN);
            catButton(fb, L.tuneUp, 101, "+", false, !s.catLink, BTN);
            catButton(fb, L.stepBtn, 102, ("Paso " + fmtStep(s.step)).c_str(), false, false, BTN);
            for (int i = 0; i < 6; i++) catButton(fb, L.modeBtn[i], 110 + i, CAT_MODES[i], s.mode == CAT_MODES[i] || (i == 5 && s.mode.rfind("PKT", 0) == 0), !s.catLink, SEL);
            if (!L.land) fb.text(L.W / 2, 378, s.rxOnly ? "Receptor: sin PTT" : (s.allowPtt ? "PTT: mantener pulsado para emitir" : "PTT desactivado (cat.ptt)"), S, MUT, CENTER);
            catButton(fb, L.trackBtn, 103, "Seguir", s.tracking, !s.catLink, SEL);
            catButton(fb, L.vfoBtn, 104, "A / B", false, !s.catLink || s.rxOnly, BTN);
            catButton(fb, L.pttBtn, 105, "PTT", s.ptt, !(s.allowPtt && s.catLink), STOPC);
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
            card(fb, ic, CARD);
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
            card(fb, ac, CARD);
            int ay = ac.y + 4;
            fb.text(ac.x + 8, ay, "Acerca de", M, MUT);
            ay += M.height + 4;
            const int aw = ac.w - 16, lh = S.height + (L.land ? 0 : 4);
            auto para = [&](const std::string& txt, uint16_t c) { for (const std::string& l : wrap(S, txt, aw)) { fb.text(ac.x + 8, ay, l.c_str(), S, c); ay += lh; } };
            para("APRS LoRa+ Rotor " + s.ver, FG);
            para("© 2026 Manuel - EA7JRS", FG);
            para("github.com/EA7JRS/APRS-LoRa-Plus", ACC);
            para("GPL-3.0-or-later · DejaVu Sans", MUT);

            // Brillo
            box(fb, L.brMinus.x, L.brMinus.y, L.brMinus.w, L.brMinus.h, (int)(10 * L.k), BTN, BTNB);
            box(fb, L.brPlus.x, L.brPlus.y, L.brPlus.w, L.brPlus.h, (int)(10 * L.k), BTN, BTNB);
            fb.text(L.brMinus.x + L.brMinus.w / 2, L.brMinus.y + (L.brMinus.h - M.height) / 2, "−", M, BTNT, CENTER);
            fb.text(L.brPlus.x + L.brPlus.w / 2, L.brPlus.y + (L.brPlus.h - M.height) / 2, "+", M, BTNT, CENTER);
            snprintf(t, sizeof t, "Brillo %d%%", s.bright);
            fb.text(L.brTextX, L.brTextY, t, S, FG, CENTER);
        }
    };
}
