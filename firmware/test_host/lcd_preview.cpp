/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 *
 * Pantalla táctil en el PC: comprueba los toques y dibuja las pantallas en ficheros PPM (lcd_out/) para verlas.
 */

#include <cstdio>
#include <vector>
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

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "lcd_out";
    std::vector<uint16_t> buf(Lcd::Ui::W * Lcd::Ui::H);
    Lcd::Fb fb;
    fb.attach(buf.data(), Lcd::Ui::W, Lcd::Ui::H);
    Lcd::Ui ui;

    Lcd::UiState s;
    s.ver = "5.3.4"; s.name = "APRS-Rotor"; s.call = "EA7JRS-12"; s.board = "waveshare_c5_lcd35"; s.chip = "ESP32-C5";
    s.drv = "direct (relays)"; s.ip = "192.168.1.57"; s.wifi = "Casa (-61 dBm)"; s.rotctld = 4533; s.apiPort = 4534;
    s.ble = true; s.bleConn = true; s.upS = 3 * 3600 + 25 * 60; s.maxAz = 450;
    s.az = 123.4; s.el = 32.5; s.taz = 210.0; s.tel = 45.0; s.maz = 123.4; s.moving = true;

    // ---- toques: botones
    Lcd::UiCmd c = ui.touch(true, 50, 390, 1000, s);         // CCW
    CHECK(c.cmd == Lcd::Cmd::MOVE && c.dir == 8);
    ui.setFrameTime(1100);
    ui.draw(fb, s);
    writePpm(fb, out + "/main_ccw_pressed.ppm");
    c = ui.touch(true, 52, 392, 1200, s);
    CHECK(c.cmd == Lcd::Cmd::NONE);
    c = ui.touch(false, 0, 0, 1500, s);
    CHECK(c.cmd == Lcd::Cmd::STOP);
    c = ui.touch(true, 270, 390, 2000, s);                   // CW
    CHECK(c.cmd == Lcd::Cmd::MOVE && c.dir == 16);
    c = ui.touch(false, 0, 0, 2100, s);
    CHECK(c.cmd == Lcd::Cmd::STOP);
    c = ui.touch(true, 160, 390, 3000, s);                   // STOP
    CHECK(c.cmd == Lcd::Cmd::STOP);
    c = ui.touch(false, 0, 0, 3050, s);
    CHECK(c.cmd == Lcd::Cmd::NONE);
    c = ui.touch(true, 160, 450, 3100, s);                   // PARK
    CHECK(c.cmd == Lcd::Cmd::PARK);
    ui.touch(false, 0, 0, 3150, s);
    c = ui.touch(true, 270, 450, 3200, s);                   // UP
    CHECK(c.cmd == Lcd::Cmd::MOVE && c.dir == 2);
    ui.touch(false, 0, 0, 3300, s);
    c = ui.touch(true, 50, 450, 3400, s);                    // DOWN
    CHECK(c.cmd == Lcd::Cmd::MOVE && c.dir == 4);
    ui.touch(false, 0, 0, 3500, s);
    s.hasEl = false;
    c = ui.touch(true, 270, 450, 3600, s);                   // UP sin elevación: nada
    CHECK(c.cmd == Lcd::Cmd::NONE);
    c = ui.touch(false, 0, 0, 3700, s);
    CHECK(c.cmd == Lcd::Cmd::NONE);
    s.hasEl = true;

    // ---- brújula: toque corto no hace nada; mantenido y soltado = ir a
    c = ui.touch(true, 160 + 80, 154, 4000, s);              // este
    ui.touch(false, 0, 0, 4100, s);
    CHECK(c.cmd == Lcd::Cmd::NONE);
    ui.touch(true, 160 + 80, 154, 5000, s);
    ui.touch(true, 160, 154 + 80, 5300, s);                  // arrastra al sur
    ui.setFrameTime(5600);
    CHECK(ui.aiming(5600));
    ui.draw(fb, s);
    writePpm(fb, out + "/main_aiming.ppm");
    c = ui.touch(false, 0, 0, 5700, s);
    CHECK(c.cmd == Lcd::Cmd::GOTO && fabs(c.az - 180) < 0.01);
    ui.touch(true, 160 - 70, 154 - 70, 6000, s);             // noroeste
    c = ui.touch(false, 0, 0, 6600, s);
    CHECK(c.cmd == Lcd::Cmd::GOTO && fabs(c.az - 315) < 0.01);

    // ---- reposo, fallo, sin elevación
    s.moving = false; s.taz = NAN; s.tel = NAN;
    ui.setFrameTime(7000);
    ui.draw(fb, s);
    writePpm(fb, out + "/main_idle.ppm");
    s.fault = 1; s.faultText = "STALL AZ";
    ui.draw(fb, s);
    writePpm(fb, out + "/main_fault.ppm");
    c = ui.touch(true, 160, 340, 7100, s);
    CHECK(c.cmd == Lcd::Cmd::CLEAR_FAULT);
    ui.touch(false, 0, 0, 7200, s);
    s.fault = 0; s.faultText = "OK";
    s.hasEl = false; s.el = NAN; s.az = 359.9; s.maz = 380; s.taz = 20;
    ui.draw(fb, s);
    writePpm(fb, out + "/main_az_only.ppm");
    s.hasEl = true; s.el = 32.5; s.maz = 123.4;

    // ---- información
    c = ui.touch(true, 292, 24, 8000, s);
    ui.touch(false, 0, 0, 8100, s);
    CHECK(ui.page == Lcd::Ui::INFO);
    ui.draw(fb, s);
    writePpm(fb, out + "/info.ppm");
    c = ui.touch(true, 50, 440, 8200, s);
    CHECK(c.cmd == Lcd::Cmd::BRIGHT && c.value == 70);
    ui.touch(false, 0, 0, 8300, s);
    c = ui.touch(true, 260, 440, 8400, s);
    CHECK(c.cmd == Lcd::Cmd::BRIGHT && c.value == 90);
    ui.touch(false, 0, 0, 8500, s);
    ui.touch(true, 292, 24, 8600, s);
    ui.touch(false, 0, 0, 8700, s);
    CHECK(ui.page == Lcd::Ui::MAIN);

    // ---- texto: anchos y glifos que faltan
    CHECK(Lcd::Fb::textWidth(FontS, "°") > 0);
    CHECK(Lcd::Fb::textWidth(FontS, "Elevación") > Lcd::Fb::textWidth(FontS, "Elevacion") - 2);
    CHECK(Lcd::Fb::textWidth(FontL, "359.9°") < 160);

    printf("%d comprobaciones de la pantalla, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
