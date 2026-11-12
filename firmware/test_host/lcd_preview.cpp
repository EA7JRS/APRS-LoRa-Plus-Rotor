/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 *
 * Pantalla táctil en el PC: comprueba los toques en las dos disposiciones (vertical 320 x 480 y apaisadas 600 x 450 y 1024 x 600)
 * y dibuja las pantallas en ficheros PPM (lcd_out/) para verlas.
 */

#include <cstdio>
#include <vector>
#include <algorithm>
#include <string>
#include "../src/lcd_ui.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FALLO %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void writePpm(const Lcd::Fb& fb, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("no se puede escribir %s\n", path.c_str()); return; }
    fprintf(f, "P6\n%d %d\n255\n", fb.w, fb.h);
    for (int i = 0; i < fb.w * fb.h; i++) {
        const uint16_t v = Lcd::swap16(fb.px[i]);
        const uint8_t r = (uint8_t)(((v >> 11) & 31) * 255 / 31), g = (uint8_t)(((v >> 5) & 63) * 255 / 63), b = (uint8_t)((v & 31) * 255 / 31);
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
}

static void center(const Lcd::Rect& r, int& x, int& y) { x = r.x + r.w / 2; y = r.y + r.h / 2; }

static void run(int W, int H, const std::string& out, int theme = Lcd::THEME_APP) {
    std::vector<uint16_t> buf(W * H);
    Lcd::Fb fb;
    fb.attach(buf.data(), W, H);
    Lcd::Ui ui(W, H, theme);
    const Lcd::Layout& L = ui.L;
    char pre[16];
    snprintf(pre, sizeof pre, "%s%dx%d_", theme == Lcd::THEME_ROTOR ? "rotor_" : "", W, H);
    const std::string p = out + "/" + pre;

    Lcd::UiState s;
    s.ver = "5.4.2"; s.name = "APRS-Rotor"; s.call = "EA7JRS-12"; s.board = W > H ? "waveshare_s3_amoled241" : "waveshare_c5_lcd35";
    s.chip = W > H ? "ESP32-S3" : "ESP32-C5";
    s.drv = "direct (relays)"; s.ip = "192.168.1.57"; s.wifi = "Casa (-61 dBm)"; s.rotctld = 4533; s.apiPort = 4534;
    s.ble = true; s.bleConn = true; s.upS = 3 * 3600 + 25 * 60; s.maxAz = 450;
    s.az = 123.4; s.el = 32.5; s.taz = 210.0; s.tel = 45.0; s.maz = 123.4; s.moving = true;

    // ---- pantalla de arranque: el logo cabe entero, fondo #131A22 en las esquinas, texto debajo dentro de la pantalla
    ui.splash(fb, "5.4.2");
    writePpm(fb, p + "splash.ppm");
    const uint16_t sbg = Lcd::hex(LCD_SPLASH_BG);
    CHECK(fb.get(0, 0) == sbg && fb.get(W - 1, H - 1) == sbg);
    CHECK(LCD_SPLASH_W <= W && LCD_SPLASH_H + L.S->height + 6 <= H);
    const double sk = std::max(1.0, (double)(H - L.S->height - 18) / LCD_SPLASH_H);
    const int sh = (int)(LCD_SPLASH_H * sk);
    {
        int lit = 0;
        for (int yy = 0; yy < H; yy++) for (int xx = 0; xx < W; xx++) if (fb.get(xx, yy) != sbg) lit++;
        CHECK(lit > LCD_SPLASH_W * LCD_SPLASH_H / 4);
        CHECK(fb.get(W / 2, (H - sh - L.S->height - 6) / 2 + sh / 2) != sbg);
        int below = 0;                                            // "Rotor 5.4.2" bajo el logo
        const int y0 = (H - sh - L.S->height - 6) / 2 + sh + 4;
        for (int yy = y0; yy < y0 + L.S->height && yy < H; yy++) for (int xx = 0; xx < W; xx++) if (fb.get(xx, yy) != sbg) below++;
        CHECK(below > 20);
    }

    int x, y;
    // ---- botones: CCW mantenido, soltar = STOP
    center(L.btn[0], x, y);
    Lcd::UiCmd c = ui.touch(true, x, y, 1000, s);
    CHECK(c.cmd == Lcd::Cmd::MOVE && c.dir == 8);
    ui.setFrameTime(1100);
    ui.draw(fb, s);
    writePpm(fb, p + "main_ccw_pressed.ppm");
    c = ui.touch(true, x + 2, y + 2, 1200, s);
    CHECK(c.cmd == Lcd::Cmd::NONE);
    c = ui.touch(false, 0, 0, 1500, s);
    CHECK(c.cmd == Lcd::Cmd::STOP);
    const int dirs[6] = { 8, 0, 16, 4, 0, 2 };
    for (int i = 0; i < 6; i++) {
        center(L.btn[i], x, y);
        c = ui.touch(true, x, y, 2000 + i * 100, s);
        if (dirs[i]) CHECK(c.cmd == Lcd::Cmd::MOVE && c.dir == dirs[i]);
        else CHECK(c.cmd == (i == 1 ? Lcd::Cmd::STOP : Lcd::Cmd::PARK));
        c = ui.touch(false, 0, 0, 2050 + i * 100, s);
        CHECK(c.cmd == (dirs[i] ? Lcd::Cmd::STOP : Lcd::Cmd::NONE));
    }
    s.hasEl = false;
    center(L.btn[5], x, y);
    c = ui.touch(true, x, y, 3600, s);                       // UP sin elevación: nada
    CHECK(c.cmd == Lcd::Cmd::NONE);
    c = ui.touch(false, 0, 0, 3700, s);
    CHECK(c.cmd == Lcd::Cmd::NONE);
    s.hasEl = true;

    // ---- brújula: toque corto no hace nada; mantenido y soltado = ir a
    const int d = L.r * 8 / 10;
    c = ui.touch(true, L.cx + d, L.cy, 4000, s);             // este
    ui.touch(false, 0, 0, 4100, s);
    CHECK(c.cmd == Lcd::Cmd::NONE);
    ui.touch(true, L.cx + d, L.cy, 5000, s);
    ui.touch(true, L.cx, L.cy + d, 5300, s);                 // arrastra al sur
    ui.setFrameTime(5600);
    CHECK(ui.aiming(5600));
    ui.draw(fb, s);
    writePpm(fb, p + "main_aiming.ppm");
    c = ui.touch(false, 0, 0, 5700, s);
    CHECK(c.cmd == Lcd::Cmd::GOTO && fabs(c.az - 180) < 0.01);
    ui.touch(true, L.cx - d * 7 / 10, L.cy - d * 7 / 10, 6000, s);   // noroeste
    c = ui.touch(false, 0, 0, 6600, s);
    CHECK(c.cmd == Lcd::Cmd::GOTO && fabs(c.az - 315) < 0.01);

    // ---- reposo, fallo, sin elevación
    s.moving = false; s.taz = NAN; s.tel = NAN;
    ui.setFrameTime(7000);
    ui.draw(fb, s);
    writePpm(fb, p + "main_idle.ppm");
    s.fault = 1; s.faultText = "STALL AZ";
    ui.draw(fb, s);
    writePpm(fb, p + "main_fault.ppm");
    center(L.status, x, y);
    c = ui.touch(true, x, y, 7100, s);
    CHECK(c.cmd == Lcd::Cmd::CLEAR_FAULT);
    ui.touch(false, 0, 0, 7200, s);
    // aparcado por viento: tarjeta roja y toque = liberar
    s.fault = 6; s.faultText = "WIND"; s.wind = "20/28 m/s";
    ui.draw(fb, s);
    writePpm(fb, p + "main_wind.ppm");
    CHECK(fb.get(L.status.x + 3, L.status.y + L.status.h / 2) == ui.FAULT_BG);
    { center(L.status, x, y); Lcd::UiCmd cw = ui.touch(true, x, y, 7400, s); CHECK(cw.cmd == Lcd::Cmd::CLEAR_FAULT); ui.touch(false, 0, 0, 7450, s); }
    s.wind.clear();
    s.fault = 0; s.faultText = "OK";
    s.hasEl = false; s.el = NAN; s.az = 359.9; s.maz = 380; s.taz = 20; s.moving = true;
    ui.draw(fb, s);
    writePpm(fb, p + "main_az_only.ppm");
    s.hasEl = true; s.el = 32.5; s.maz = 123.4;

    // ---- información
    ui.touch(true, L.infoX, L.infoY, 8000, s);
    ui.touch(false, 0, 0, 8100, s);
    CHECK(ui.page == Lcd::Ui::INFO);
    ui.draw(fb, s);
    writePpm(fb, p + "info.ppm");
    center(L.brMinus, x, y);
    c = ui.touch(true, x, y, 8200, s);
    CHECK(c.cmd == Lcd::Cmd::BRIGHT && c.value == 70);
    ui.touch(false, 0, 0, 8300, s);
    center(L.brPlus, x, y);
    c = ui.touch(true, x, y, 8400, s);
    CHECK(c.cmd == Lcd::Cmd::BRIGHT && c.value == 90);
    ui.touch(false, 0, 0, 8500, s);
    ui.touch(true, L.infoX, L.infoY, 8600, s);
    ui.touch(false, 0, 0, 8700, s);
    CHECK(ui.page == Lcd::Ui::MAIN);

    // ---- CAT
    ui.touch(true, L.catBtn.x + 5, L.catBtn.y + 5, 9000, s);    // sin CAT configurado: nada
    ui.touch(false, 0, 0, 9050, s);
    CHECK(ui.page == Lcd::Ui::MAIN);
    s.catOn = true; s.catLink = true; s.rig = "IC-9700 · CI-V A2 · 19200"; s.freq = 145825000; s.mode = "FM"; s.vfo = 'A';
    s.smeter = 7; s.step = 5000; s.allowPtt = true; s.tracking = true; s.trackName = "ISS"; s.doppler = 2340;
    s.txFreq = 145990000; s.txMode = "FM"; s.tone = 670;
    ui.draw(fb, s);
    writePpm(fb, p + "main_with_cat.ppm");
    center(L.catBtn, x, y);
    ui.touch(true, x, y, 9100, s);
    ui.touch(false, 0, 0, 9150, s);
    CHECK(ui.page == Lcd::Ui::CAT);
    ui.draw(fb, s);
    writePpm(fb, p + "cat_sat.ppm");
    center(L.tuneUp, x, y);
    c = ui.touch(true, x, y, 9200, s);
    CHECK(c.cmd == Lcd::Cmd::CAT && c.cat == Lcd::CatOp::TUNE && c.value == 1);
    ui.touch(false, 0, 0, 9250, s);
    center(L.tuneDown, x, y);
    c = ui.touch(true, x, y, 9300, s);
    CHECK(c.cmd == Lcd::Cmd::CAT && c.cat == Lcd::CatOp::TUNE && c.value == -1);
    ui.touch(false, 0, 0, 9350, s);
    center(L.stepBtn, x, y);
    c = ui.touch(true, x, y, 9400, s);
    CHECK(c.cat == Lcd::CatOp::STEP && c.value == 12500);
    ui.touch(false, 0, 0, 9450, s);
    center(L.modeBtn[1], x, y);
    c = ui.touch(true, x, y, 9500, s);
    CHECK(c.cat == Lcd::CatOp::MODE && c.text == "USB");
    ui.touch(false, 0, 0, 9550, s);
    center(L.trackBtn, x, y);
    c = ui.touch(true, x, y, 9600, s);
    CHECK(c.cat == Lcd::CatOp::TRACK && c.value == 0);
    ui.touch(false, 0, 0, 9650, s);
    center(L.pttBtn, x, y);
    c = ui.touch(true, x, y, 9700, s);
    CHECK(c.cat == Lcd::CatOp::PTT_ON);
    s.ptt = true;
    ui.draw(fb, s);
    writePpm(fb, p + "cat_tx.ppm");
    c = ui.touch(false, 0, 0, 9900, s);
    CHECK(c.cmd == Lcd::Cmd::CAT && c.cat == Lcd::CatOp::PTT_OFF);
    s.ptt = false; s.allowPtt = false;
    c = ui.touch(true, x, y, 10000, s);                      // PTT desactivado
    CHECK(c.cmd == Lcd::Cmd::NONE);
    ui.touch(false, 0, 0, 10050, s);
    s.tracking = false; s.txFreq = 0; s.freq = 14074000; s.mode = "USB"; s.smeter = 12; s.step = 100; s.rig = "FT-991A · Yaesu · 38400";
    ui.draw(fb, s);
    writePpm(fb, p + "cat_hf.ppm");
    for (int i = 0; i < 6; i++) CHECK(L.modeBtn[i].x + L.modeBtn[i].w <= W && L.modeBtn[i].y + L.modeBtn[i].h <= H);
    CHECK(L.pttBtn.y + L.pttBtn.h <= H && L.trackCard.y + L.trackCard.h <= L.tuneDown.y);
    CHECK(Lcd::Ui::fmtFreqPublic(145825000) == "145.825.000" && Lcd::Ui::fmtFreqPublic(7074000) == "7.074.000");
    center(L.catBtn, x, y);
    ui.touch(true, x, y, 10100, s);
    ui.touch(false, 0, 0, 10150, s);
    CHECK(ui.page == Lcd::Ui::MAIN);
    s.catOn = false;

    // ---- todo dentro de la pantalla
    for (int i = 0; i < 6; i++) CHECK(L.btn[i].x >= 0 && L.btn[i].x + L.btn[i].w <= W && L.btn[i].y + L.btn[i].h <= H);
    CHECK(L.cy + L.tickLabelR + L.S->height / 2 <= L.btn[0].y);
    if (L.cy + L.tickLabelR + L.S->height / 2 > L.btn[0].y) printf("%dx%d: brujula %d > botones %d\n", W, H, L.cy + L.tickLabelR + L.S->height / 2, L.btn[0].y);
    CHECK(Lcd::Fb::textWidth(*L.L, "359.9°") < (W > H ? W - L.azLx : 160));
}

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "lcd_out";
    run(320, 480, out);
    run(600, 450, out);
    run(1024, 600, out);
    run(320, 480, out, Lcd::THEME_ROTOR);
    run(600, 450, out, Lcd::THEME_ROTOR);
    run(1024, 600, out, Lcd::THEME_ROTOR);
    {   // paletas: la de la app de serie; cambio en caliente
        Lcd::Ui u(320, 480);
        CHECK(u.theme == Lcd::THEME_APP && u.BG == Lcd::hex(0x131A22) && u.HDR == Lcd::hex(0x131A22) && u.SEL == Lcd::hex(0xFFB347));
        u.setTheme(Lcd::THEME_ROTOR);
        CHECK(u.HDR == Lcd::hex(0xb21d1d) && u.BG == Lcd::hex(0x10151c));
        u.setTheme(7);
        CHECK(u.theme == Lcd::THEME_APP && u.ACC == Lcd::hex(0x28B5E1));
        // S-meter como el de la app: S7 = 7 segmentos con su color, los demás al 15 %, lectura en el amarillo de S7
        std::vector<uint16_t> b(300 * 40);
        Lcd::Fb f; f.attach(b.data(), 300, 40); f.fill(u.BG);
        u.sMeter(f, { 0, 0, 300, 40 }, 7);
        auto has = [&](uint32_t c) { const uint16_t v = Lcd::hex(c); for (uint16_t p : b) if (p == v) return true; return false; };
        CHECK(has(Lcd::S_COLORS[0]) && has(Lcd::S_COLORS[6]) && !has(Lcd::S_COLORS[7]) && has(Lcd::mix(0x131A22, Lcd::S_COLORS[7], 0.15))
              && has(Lcd::mix(0x131A22, Lcd::S_COLORS[14], 0.15)) && !has(Lcd::S_COLORS[14]));
        f.fill(u.BG); u.sMeter(f, { 0, 0, 300, 40 }, 15);
        CHECK(has(Lcd::S_COLORS[14]) && has(Lcd::S_COLORS[9]));
        CHECK(Lcd::mix(0x000000, 0xFFFFFF, 0.15) == 0x262626);
    }
    // Texto
    CHECK(Lcd::Fb::textWidth(FontS, "°") > 0);
    CHECK(Lcd::Ui::wrap(FontS, "uno dos tres cuatro", Lcd::Fb::textWidth(FontS, "uno dos")).size() == 3);
    CHECK(Lcd::Ui::fit(FontS, "texto muy largo para el sitio", 60).find("…") != std::string::npos);
    CHECK(Lcd::Ui::fit(FontS, "corto", 200) == "corto");
    printf("%d comprobaciones de la pantalla, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
