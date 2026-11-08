/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "lcd_hw.h"
#if ROTOR_HAS_LCD
#include <SPI.h>
#include <initializer_list>
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

// Pantallas: 1 = ST7796 por SPI, 320 x 480 vertical (ESP32-C5-Touch-LCD-3.5)
//            2 = RM690B0 AMOLED por QSPI, 600 x 450 apaisada (ESP32-S3-Touch-AMOLED-2.41)
#ifndef LCD_PANEL
    #define LCD_PANEL 1
#endif
#if LCD_PANEL == 2
    #include <driver/spi_master.h>
    #include <esp_lcd_panel_io.h>
    #define LCD_W 600
    #define LCD_H 450
    #ifndef LCD_QSPI_CS
        #define LCD_QSPI_CS 9
    #endif
    #ifndef LCD_QSPI_CLK
        #define LCD_QSPI_CLK 10
    #endif
    #ifndef LCD_QSPI_D0
        #define LCD_QSPI_D0 11
    #endif
    #ifndef LCD_QSPI_D1
        #define LCD_QSPI_D1 12
    #endif
    #ifndef LCD_QSPI_D2
        #define LCD_QSPI_D2 13
    #endif
    #ifndef LCD_QSPI_D3
        #define LCD_QSPI_D3 14
    #endif
    #define LCD_Y_OFFSET 16             // la memoria del panel empieza 16 filas más abajo (ejemplo de Waveshare)
    #define LCD_DIM_PCT 0               // AMOLED: sin uso se apaga (evita marcas en la pantalla)
#else
    #define LCD_W 320
    #define LCD_H 480
    #define LCD_DIM_PCT 10
#endif

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
    static const int W = LCD_W, H = LCD_H, BAND = 16, BANDS = (H + BAND - 1) / BAND;
    static const uint8_t TOUCH_ADDR = 0x38;

    static bool         ok = false;
    static uint16_t*    draw_ = nullptr;          // donde se dibuja
    static uint16_t*    shown_ = nullptr;         // lo que hay en la pantalla
    static Lcd::Fb      fb;
    static Lcd::Ui      ui(LCD_W, LCD_H);
    static Lcd::UiState st;
    static int          nextBand = -1;            // -1: nada pendiente
    static uint32_t     lastDraw = 0, lastTouch = 0, lastActivity = 0, brightSaveAt = 0;
    static bool         touchOk = false, dimmed = false, swallow = false, firstFrame = true;

#if LCD_PANEL == 2
    // ------------------------------------------------------------------------------------------------ RM690B0 (QSPI)
    // Órdenes: 0x02 + orden en un hilo; puntos: 0x32 + 0x2C en cuatro hilos (como esp_lcd_sh8601 de Waveshare)
    static esp_lcd_panel_io_handle_t io = nullptr;
    static uint16_t*      dmaBuf = nullptr;         // franja en memoria interna con DMA
    static volatile bool  txDone = true;

    static bool onDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) { txDone = true; return false; }

    static void cmd(uint8_t c, const uint8_t* d = nullptr, size_t n = 0) {
        esp_lcd_panel_io_tx_param(io, (int)((0x02u << 24) | ((uint32_t)c << 8)), d, n);
    }
    static void cmdList(uint8_t c, std::initializer_list<uint8_t> d) { cmd(c, d.begin(), d.size()); }

    static void setBright(int pct) { cmdList(0x51, { (uint8_t)(pct * 255 / 100) }); }

    static void waitTx() { const uint32_t t0 = millis(); while (!txDone && millis() - t0 < 200) delay(0); }

    static void pushRows(int y0, int rows) {
        waitTx();
        const int y = y0 + LCD_Y_OFFSET, y1 = y + rows - 1;
        cmdList(0x2A, { 0, 0, (uint8_t)((W - 1) >> 8), (uint8_t)(W - 1) });
        cmdList(0x2B, { (uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8), (uint8_t)y1 });
        memcpy(dmaBuf, shown_ + y0 * W, (size_t)rows * W * 2);
        txDone = false;
        if (esp_lcd_panel_io_tx_color(io, (int)((0x32u << 24) | (0x2Cu << 8)), dmaBuf, (size_t)rows * W * 2) != ESP_OK) txDone = true;
    }

    static bool panelInit() {
        dmaBuf = (uint16_t*)heap_caps_malloc((size_t)W * BAND * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!dmaBuf) { logf("lcd: sin memoria DMA\n"); return false; }
        spi_bus_config_t bus = {};
        bus.sclk_io_num = LCD_QSPI_CLK;
        bus.data0_io_num = LCD_QSPI_D0;
        bus.data1_io_num = LCD_QSPI_D1;
        bus.data2_io_num = LCD_QSPI_D2;
        bus.data3_io_num = LCD_QSPI_D3;
        bus.max_transfer_sz = W * BAND * 2 + 64;
        if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) { logf("lcd: bus QSPI\n"); return false; }
        esp_lcd_panel_io_spi_config_t ioc = {};
        ioc.cs_gpio_num = LCD_QSPI_CS;
        ioc.dc_gpio_num = -1;
        ioc.spi_mode = 0;
        ioc.pclk_hz = 40 * 1000 * 1000;
        ioc.trans_queue_depth = 4;
        ioc.on_color_trans_done = onDone;
        ioc.lcd_cmd_bits = 32;
        ioc.lcd_param_bits = 8;
        ioc.flags.quad_mode = 1;
        if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io) != ESP_OK) { logf("lcd: QSPI\n"); return false; }
        // Secuencia del ejemplo de Waveshare (09_LVGL_Test, girada 90°: MADCTL 0x30)
        cmd(0x01); delay(80);
        cmdList(0x36, { 0x00 });
        cmdList(0x3A, { 0x55 });
        cmdList(0xFE, { 0x20 });
        cmdList(0x26, { 0x0A });
        cmdList(0x24, { 0x80 });
        cmdList(0xFE, { 0x00 });
        cmdList(0x3A, { 0x55 });
        cmdList(0xC2, { 0x00 }); delay(10);
        cmd(0x35);
        cmdList(0x51, { 0x00 }); delay(10);
        cmd(0x11); delay(80);
        cmdList(0x2A, { 0x00, 0x10, 0x00, 0xD1 });
        cmdList(0x2B, { 0x00, 0x00, 0x00, 0x57 });
        cmd(0x29); delay(10);
        cmdList(0x36, { 0x30 });
        return true;
    }

    static void busPins() {}
#else
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

    static void setBright(int pct) { Exio::backlight((uint8_t)pct); }

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

    static bool panelInit() {
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
        return true;
    }

    static void busPins() {
        pinMode(LCD_SD_CS, OUTPUT); digitalWrite(LCD_SD_CS, HIGH);
        pinMode(LCD_CS, OUTPUT);    digitalWrite(LCD_CS, HIGH);
        pinMode(LCD_DC, OUTPUT);    digitalWrite(LCD_DC, HIGH);
        SPI.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, -1);
    }

#endif

    // ------------------------------------------------------------------------------------------------ FT6336
    static bool touchRead(int& x, int& y) {
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write((uint8_t)0x02);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom(TOUCH_ADDR, (uint8_t)5) != 5) return false;
        uint8_t d[5];
        for (int i = 0; i < 5; i++) d[i] = Wire.read();
        if ((d[0] & 0x0F) == 0) return false;
        const int rx = ((d[1] & 0x0F) << 8) | d[2], ry = ((d[3] & 0x0F) << 8) | d[4];
    #if LCD_PANEL == 2
        x = (W - 1) - ry;           // el táctil va en vertical: espejo de Y y cambio de ejes (ejemplo de Waveshare)
        y = rx;
    #else
        x = rx; y = ry;
    #endif
        return x >= 0 && y >= 0 && x < W && y < H;
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

    static void applyBright() { setBright(dimmed ? LCD_DIM_PCT : Cfg.lcdBright); }

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
        busPins();
        if (!panelInit()) return;
        Wire.beginTransmission(TOUCH_ADDR);
        touchOk = Wire.endTransmission() == 0;
        if (!touchOk) logf("lcd: no hay táctil FT6336\n");
        fb.attach(draw_, W, H);
        memset(shown_, 0, (size_t)W * H * 2);
        ok = true;
        lastActivity = millis();
        logf("lcd: %s %dx%d%s\n", LCD_PANEL == 2 ? "RM690B0" : "ST7796", W, H, touchOk ? " + FT6336" : "");
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
                const int rows = H - nextBand * BAND < BAND ? H - nextBand * BAND : BAND;
                if (memcmp(draw_ + off, shown_ + off, (size_t)rows * W * 2) != 0) {
                    memcpy(shown_ + off, draw_ + off, (size_t)rows * W * 2);
                    pushRows(nextBand * BAND, rows);
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
