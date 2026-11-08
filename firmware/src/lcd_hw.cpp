/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "lcd_hw.h"
#if ROTOR_HAS_LCD
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include "lcd_ui.h"
#include "exio.h"
#include "config.h"
#include "drivers.h"
#include "net.h"
#include "bt.h"
#include "lora_aprs.h"
#include "rc_common.h"
#include "rc_serialproto.h"

#ifndef LCD_MOSI
    #define LCD_MOSI 7
#endif
#ifndef LCD_SCLK
    #define LCD_SCLK 6
#endif
#ifndef LCD_MISO
    #define LCD_MISO 2          // compartido con la microSD (no se usa)
#endif
#ifndef LCD_CS
    #define LCD_CS 8
#endif
#ifndef LCD_DC
    #define LCD_DC 5
#endif
#ifndef LCD_SD_CS
    #define LCD_SD_CS 9         // la microSD comparte el bus: su CS arriba
#endif
#ifndef LCD_SPI_HZ
    #define LCD_SPI_HZ 40000000
#endif

namespace LcdHw {
    static const int W = Lcd::Ui::W, H = Lcd::Ui::H, BAND = 16, BANDS = H / BAND;
    static const uint8_t TOUCH_ADDR = 0x38;

    static bool         ok = false;
    static uint16_t*    draw_ = nullptr;          // donde se dibuja
    static uint16_t*    shown_ = nullptr;         // lo que hay en la pantalla
    static Lcd::Fb      fb;
    static Lcd::Ui      ui;
    static Lcd::UiState st;
    static int          nextBand = -1;            // -1: nada pendiente
    static uint32_t     lastDraw = 0, lastTouch = 0, lastActivity = 0, brightSaveAt = 0;
    static bool         touchOk = false, dimmed = false, swallow = false, firstFrame = true;

    // ------------------------------------------------------------------------------------------------ ST7796
    static void cmd(uint8_t c, const uint8_t* d = nullptr, size_t n = 0) {
        SPI.beginTransaction(SPISettings(LCD_SPI_HZ, MSBFIRST, SPI_MODE0));
        digitalWrite(LCD_CS, LOW);
        digitalWrite(LCD_DC, LOW);
        SPI.transfer(c);
        if (n) { digitalWrite(LCD_DC, HIGH); SPI.writeBytes(d, n); }
        digitalWrite(LCD_CS, HIGH);
        SPI.endTransaction();
    }
    static void cmdList(uint8_t c, std::initializer_list<uint8_t> d) { cmd(c, d.begin(), d.size()); }

    static void window(int x0, int y0, int x1, int y1) {
        cmdList(0x2A, { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 });
        cmdList(0x2B, { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 });
    }

    static void pushRows(int y0, int rows) {
        window(0, y0, W - 1, y0 + rows - 1);
        SPI.beginTransaction(SPISettings(LCD_SPI_HZ, MSBFIRST, SPI_MODE0));
        digitalWrite(LCD_CS, LOW);
        digitalWrite(LCD_DC, LOW);
        SPI.transfer(0x2C);
        digitalWrite(LCD_DC, HIGH);
        SPI.writeBytes((const uint8_t*)(shown_ + y0 * W), (uint32_t)rows * W * 2);
        digitalWrite(LCD_CS, HIGH);
        SPI.endTransaction();
    }

    static void panelInit() {
        // Secuencia del ejemplo de Waveshare (07_lvgl_demo / BSP): BGR + espejo X, RGB565, inversión
        cmd(0x01); delay(20);
        cmd(0x11); delay(100);
        cmdList(0x36, { 0x48 });
        cmdList(0x3A, { 0x55 });
        cmdList(0xF0, { 0xC3 });
        cmdList(0xF0, { 0x96 });
        cmdList(0xB4, { 0x01 });
        cmdList(0xB7, { 0xC6 });
        cmdList(0xE8, { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 });
        cmdList(0xC1, { 0x06 });
        cmdList(0xC2, { 0xA7 });
        cmdList(0xC5, { 0x18 });
        cmdList(0xE0, { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B });
        cmdList(0xE1, { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2D, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B });
        cmdList(0xF0, { 0x3C });
        cmdList(0xF0, { 0x69 });
        cmd(0x21);
        cmd(0x29);
        delay(120);
    }

    // ------------------------------------------------------------------------------------------------ FT6336
    static bool touchRead(int& x, int& y) {
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write((uint8_t)0x02);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom(TOUCH_ADDR, (uint8_t)5) != 5) return false;
        uint8_t d[5];
        for (int i = 0; i < 5; i++) d[i] = Wire.read();
        if ((d[0] & 0x0F) == 0) return false;
        x = ((d[1] & 0x0F) << 8) | d[2];
        y = ((d[3] & 0x0F) << 8) | d[4];
        return x < W && y < H;
    }

    // ------------------------------------------------------------------------------------------------ estado
    static void fillState() {
        RC::Rotor& r = Drv::rotor();
        double az = NAN, el = NAN;
        r.getPos(az, el);
        st.az = az;
        st.hasEl = r.hasEl();
        st.el = st.hasEl ? el : NAN;
        st.minAz = r.minAz(); st.maxAz = r.maxAz();
        st.moving = (r.status() & RC::ST_MOVING) != 0;
        st.canMove = !(Cfg.driver == 1 && Cfg.serProto == RC::SP_ROT2PROG);
        st.fault = Drv::fault();
        st.faultText = RC::faultText(st.fault);
        JsonDocument d;
        JsonObject o = d.to<JsonObject>();
        Drv::posExtra(o);
        st.taz = o["taz"].is<double>() ? o["taz"].as<double>() : NAN;
        st.tel = o["tel"].is<double>() ? o["tel"].as<double>() : NAN;
        st.maz = o["maz"].is<double>() ? o["maz"].as<double>() : NAN;
        st.aprs = LoraAprs::lastResult();
        st.ver = ROTOR_VERSION; st.name = Cfg.name; st.call = Cfg.aprs.callsign;
        st.board = ROTOR_BOARD; st.chip = Board::chip(); st.drv = Drv::name();
        st.ip = Net::up() ? Net::ip() : std::string();
        const wifi_mode_t m = WiFi.getMode();
        if (Cfg.wifiMode == 0) st.wifi = "apagada";
        else if (m == WIFI_AP || m == WIFI_AP_STA) st.wifi = std::string("AP ") + WiFi.softAPSSID().c_str();
        else if (WiFi.isConnected()) { char t[64]; snprintf(t, sizeof t, "%s (%d dBm)", WiFi.SSID().c_str(), (int)WiFi.RSSI()); st.wifi = t; }
        else st.wifi = "conectando...";
        st.rotctld = Cfg.rotctld ? Cfg.rotctldPort : 0;
        st.apiPort = Cfg.apiPort;
        st.ble = ROTOR_HAS_BLE && Cfg.btMode == 1;
        st.bleConn = st.ble && Bt::connected();
        st.upS = millis() / 1000;
        st.bright = Cfg.lcdBright;
    }

    static void applyBright() { Exio::backlight(dimmed ? 10 : (uint8_t)Cfg.lcdBright); }

    static void run(const Lcd::UiCmd& c) {
        RC::Rotor& r = Drv::rotor();
        switch (c.cmd) {
            case Lcd::Cmd::MOVE:  r.move(c.dir, 0); break;
            case Lcd::Cmd::STOP:  r.stop(); break;
            case Lcd::Cmd::PARK:  r.park(); break;
            case Lcd::Cmd::GOTO: {
                double az, el;
                r.getPos(az, el);
                const double tel = !std::isnan(st.tel) ? st.tel : el;
                r.setPos(c.az, r.hasEl() && !std::isnan(tel) ? tel : 0);
                break;
            }
            case Lcd::Cmd::CLEAR_FAULT: Drv::clearFault(); break;
            case Lcd::Cmd::BRIGHT:
                Cfg.lcdBright = c.value;
                applyBright();
                brightSaveAt = millis() + 3000;          // se guarda cuando deja de tocar
                break;
            case Lcd::Cmd::NONE: break;
        }
    }

    // ------------------------------------------------------------------------------------------------
    void begin() {
        if (!Exio::ok()) { logf("lcd: sin expansor de E/S\n"); return; }
        draw_ = (uint16_t*)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        shown_ = (uint16_t*)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!draw_ || !shown_) { logf("lcd: sin PSRAM para la imagen\n"); return; }
        Exio::backlight(0);
        Exio::resetLcdTouch();
        pinMode(LCD_SD_CS, OUTPUT); digitalWrite(LCD_SD_CS, HIGH);
        pinMode(LCD_CS, OUTPUT);    digitalWrite(LCD_CS, HIGH);
        pinMode(LCD_DC, OUTPUT);    digitalWrite(LCD_DC, HIGH);
        SPI.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, -1);
        panelInit();
        Wire.beginTransmission(TOUCH_ADDR);
        touchOk = Wire.endTransmission() == 0;
        if (!touchOk) logf("lcd: no hay táctil FT6336\n");
        fb.attach(draw_, W, H);
        memset(shown_, 0, (size_t)W * H * 2);
        ok = true;
        lastActivity = millis();
        logf("lcd: ST7796 320x480%s\n", touchOk ? " + FT6336" : "");
    }

    void loop() {
        if (!ok) return;
        const uint32_t now = millis();

        // Táctil cada 20 ms
        if (touchOk && now - lastTouch >= 20) {
            lastTouch = now;
            int x = 0, y = 0;
            const bool down = touchRead(x, y);
            if (down) {
                lastActivity = now;
                if (dimmed) { dimmed = false; applyBright(); swallow = true; }   // el primer toque solo enciende
            }
            if (swallow) { if (!down) swallow = false; }
            else {
                const bool was = ui.busyTouch();
                const Lcd::UiCmd c = ui.touch(down, x, y, now, st);
                run(c);
                if (c.cmd != Lcd::Cmd::NONE || down != was || (down && ui.aiming(now))) lastDraw = 0;   // redibuja ya
            }
        }

        // Bajar el brillo sin uso
        if (!dimmed && Cfg.lcdDimS > 0 && now - lastActivity > (uint32_t)Cfg.lcdDimS * 1000u) { dimmed = true; applyBright(); }
        if (brightSaveAt && (int32_t)(now - brightSaveAt) >= 0) { brightSaveAt = 0; Config::save(); }

        // Mandar a la pantalla lo que cambió: unas pocas franjas por vuelta para no frenar el control del rotor
        if (nextBand >= 0) {
            int sent = 0;
            while (nextBand < BANDS && sent < 3) {
                const size_t off = (size_t)nextBand * BAND * W;
                if (memcmp(draw_ + off, shown_ + off, (size_t)BAND * W * 2) != 0) {
                    memcpy(shown_ + off, draw_ + off, (size_t)BAND * W * 2);
                    pushRows(nextBand * BAND, BAND);
                    sent++;
                }
                nextBand++;
            }
            if (nextBand >= BANDS) {
                nextBand = -1;
                if (firstFrame) { firstFrame = false; applyBright(); }
            }
            return;
        }

        // Dibujar 5 veces por segundo (o enseguida tras un toque)
        if (lastDraw && now - lastDraw < 200) return;
        lastDraw = now ? now : 1;
        fillState();
        ui.setFrameTime(now);
        ui.draw(fb, st);
        nextBand = 0;
    }
}
#endif
