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
#include "cat.h"
#include "wind.h"          // plotter_core: beaufort()

// Pantallas: 1 = ST7796 por SPI, 320 x 480 vertical (ESP32-C5-Touch-LCD-3.5)
//            2 = RM690B0 AMOLED por QSPI, 600 x 450 apaisada (ESP32-S3-Touch-AMOLED-2.41)
//            3 = panel RGB de 16 bits, 1024 x 600 apaisada, táctil GT911 (ESP32-S3-Touch-LCD-7B)
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
#elif LCD_PANEL == 3
    #include <esp_lcd_panel_ops.h>
    #include <esp_lcd_panel_rgb.h>
    #define LCD_W 1024
    #define LCD_H 600
    #define LCD_DIM_PCT 10
    #ifndef LCD_PCLK_HZ
        #define LCD_PCLK_HZ 30000000    // el del ejemplo de Waveshare; bajarlo (p. ej. 21 MHz) si la imagen tiembla
    #endif
    #ifndef LCD_TP_INT
        #define LCD_TP_INT 4
    #endif
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
#if LCD_PANEL == 3
    static uint8_t TOUCH_ADDR = 0x5D;               // GT911: 0x5D con INT bajo en el reset; 0x14 si no
#else
    static const uint8_t TOUCH_ADDR = 0x38;
#endif

    static bool         ok = false;
    static uint16_t*    draw_ = nullptr;          // donde se dibuja
    static uint16_t*    shown_ = nullptr;         // lo que hay en la pantalla
    static Lcd::Fb      fb;
    static Lcd::Ui      ui(LCD_W, LCD_H);
    static Lcd::UiState st;
    static int          nextBand = -1;            // -1: nada pendiente
    static uint32_t     lastDraw = 0, lastTouch = 0, lastActivity = 0, brightSaveAt = 0;
    static bool         touchOk = false, dimmed = false, swallow = false, firstFrame = true;
    // Pantalla de arranque (la de bienvenida de la app): se enciende poco a poco y se queda un rato; un toque la quita
    static const uint32_t SPLASH_FADE_MS = 700, SPLASH_HOLD_MS = 1500;
    static bool         splashOn = false;
    static uint32_t     splashT0 = 0, lastFadeMs = 0;
    static int          fadePct = -1;
    static bool         splashTouched = false;

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
#elif LCD_PANEL == 3
    // ------------------------------------------------------------------------------------------------ panel RGB (LCD-7B)
    // Pines y tiempos del ejemplo 06_LCD de Waveshare (rgb_lcd_port.cpp). El ESP32-S3 refresca el panel solo desde su
    // imagen en la PSRAM; aquí solo se copian las franjas que cambian.
    static esp_lcd_panel_handle_t panel = nullptr;

    static void setBright(int pct) { Exio::backlight((uint8_t)pct); }

    static void pushRows(int y0, int rows) {
        esp_lcd_panel_draw_bitmap(panel, 0, y0, W, y0 + rows, shown_ + (size_t)y0 * W);
    }

    static bool panelInit() {
        esp_lcd_rgb_panel_config_t c = {};
        c.clk_src = LCD_CLK_SRC_DEFAULT;
        c.timings.pclk_hz = LCD_PCLK_HZ;
        c.timings.h_res = W;
        c.timings.v_res = H;
        c.timings.hsync_pulse_width = 162;
        c.timings.hsync_back_porch = 152;
        c.timings.hsync_front_porch = 48;
        c.timings.vsync_pulse_width = 45;
        c.timings.vsync_back_porch = 13;
        c.timings.vsync_front_porch = 3;
        c.timings.flags.pclk_active_neg = 1;
        c.data_width = 16;
        c.bits_per_pixel = 16;
        c.num_fbs = 1;
        c.bounce_buffer_size_px = W * 10;
        c.hsync_gpio_num = 46;
        c.vsync_gpio_num = 3;
        c.de_gpio_num = 5;
        c.pclk_gpio_num = 7;
        c.disp_gpio_num = -1;
        // B3-B7, G2-G7, R3-R7
        static const int data[16] = { 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 };
        for (int i = 0; i < 16; i++) c.data_gpio_nums[i] = data[i];
        c.flags.fb_in_psram = 1;
        if (esp_lcd_new_rgb_panel(&c, &panel) != ESP_OK) { logf("lcd: panel RGB\n"); return false; }
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        return true;
    }

    // GT911: INT bajo durante el reset del táctil -> dirección 0x5D (como el ejemplo 08_TOUCH)
    static void busPins() {
        pinMode(LCD_TP_INT, OUTPUT);
        digitalWrite(LCD_TP_INT, LOW);
    }
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

#if LCD_PANEL == 3
    // ------------------------------------------------------------------------------------------------ GT911
    static bool gtRead(uint16_t reg, uint8_t* d, uint8_t n) {
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write((uint8_t)(reg >> 8)); Wire.write((uint8_t)reg);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom(TOUCH_ADDR, n) != n) return false;
        for (int i = 0; i < n; i++) d[i] = Wire.read();
        return true;
    }
    static bool touchDown = false;
    static int  touchX = 0, touchY = 0;
    static bool touchRead(int& x, int& y) {
        uint8_t st = 0;
        if (gtRead(0x814E, &st, 1) && (st & 0x80)) {           // dato nuevo
            const int n = st & 0x0F;
            uint8_t p[6];
            touchDown = n >= 1 && n <= 5 && gtRead(0x814F, p, 6);
            if (touchDown) { touchX = p[1] | (p[2] << 8); touchY = p[3] | (p[4] << 8); }
            Wire.beginTransmission(TOUCH_ADDR);                 // borrar el aviso
            Wire.write((uint8_t)0x81); Wire.write((uint8_t)0x4E); Wire.write((uint8_t)0);
            Wire.endTransmission();
        }
        x = touchX; y = touchY;
        return touchDown && x >= 0 && y >= 0 && x < W && y < H;
    }
#else
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
#endif

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
        st.wind.clear();
        const RC::WindGuard& wg = Drv::wind();
        if (wg.have()) {
            char t[64];
            const int bf = plotter::beaufort(wg.wind() / 0.514444);
            if (wg.gust() >= 0) snprintf(t, sizeof t, "%.0f/%.0f m/s F%d", wg.wind(), wg.gust(), bf);
            else snprintf(t, sizeof t, "%.0f m/s F%d", wg.wind(), bf);
            st.wind = t;
        }
        st.sky.clear();
        const RC::SkyRotor& sk = Drv::sky();
        if (sk.tracking()) {
            char t[64];
            const char* who = sk.target() == RC::SKY_SUN ? "Sol" : "Luna";
            if (sk.below()) snprintf(t, sizeof t, "%s bajo el horizonte (%.0f°): esperando", who, sk.el());
            else snprintf(t, sizeof t, "Siguiendo %s %.1f° / %.1f°", sk.target() == RC::SKY_SUN ? "el sol" : "la luna", sk.az(), sk.el());
            for (char* p = t; *p; p++) if (*p == '.') *p = ',';
            st.sky = t;
        }
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
        // CAT
        RC::CatEngine* e = Cat::engine();
        st.catOn = e != nullptr;
        if (e) {
            const RC::CatState& c = e->state();
            st.catLink = c.link; st.rig = e->info(); st.freq = c.freq; st.txFreq = e->receiver() ? 0 : c.txFreq;
            st.mode = c.mode; st.txMode = c.txMode; st.vfo = c.vfo; st.ptt = c.ptt; st.smeter = c.smeter; st.tone = c.tone;
            st.rxOnly = e->receiver(); st.allowPtt = Cfg.catPtt && !st.rxOnly;
            st.step = Cat::step();
            const RC::CatTracker& t = Cat::tracker();
            st.tracking = t.enabled && t.active; st.trackName = t.name; st.doppler = t.doppler;
        }
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
            case Lcd::Cmd::CAT: {
                RC::CatEngine* e = Cat::engine();
                if (!e) break;
                switch (c.cat) {
                    case Lcd::CatOp::TUNE:    Cat::tune(c.value); break;
                    case Lcd::CatOp::STEP:    Cat::setStep((uint32_t)c.value); break;
                    case Lcd::CatOp::MODE:    e->setMode(c.text); break;
                    case Lcd::CatOp::VFO:     e->toggleVfo(); break;
                    case Lcd::CatOp::TRACK:   Cat::tracker().enabled = c.value != 0; Cat::tracker().reset(); break;
                    case Lcd::CatOp::PTT_ON:  if (Cfg.catPtt) e->setPtt(true); break;
                    case Lcd::CatOp::PTT_OFF: e->setPtt(false); break;
                    case Lcd::CatOp::NONE:    break;
                }
                break;
            }
            case Lcd::Cmd::NONE: break;
        }
    }

    // Unas pocas franjas cambiadas por vuelta (nextBand = -1 al terminar)
    static void pushSome() {
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
        if (nextBand >= BANDS) nextBand = -1;
    }

    // ------------------------------------------------------------------------------------------------
    void begin() {
        if (!Exio::ok()) { logf("lcd: sin expansor de E/S\n"); return; }
        draw_ = (uint16_t*)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        shown_ = (uint16_t*)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!draw_ || !shown_) { logf("lcd: sin PSRAM para la imagen\n"); return; }
        Exio::backlight(0);
        busPins();                              // antes del reset: el GT911 elige su dirección con INT
        Exio::resetLcdTouch();
        if (!panelInit()) return;
    #if LCD_PANEL == 3
        delay(50);
        pinMode(LCD_TP_INT, INPUT);
        for (uint8_t a : { (uint8_t)0x5D, (uint8_t)0x14 }) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) { TOUCH_ADDR = a; touchOk = true; break; }
        }
        if (!touchOk) logf("lcd: no hay táctil GT911\n");
    #else
        Wire.beginTransmission(TOUCH_ADDR);
        touchOk = Wire.endTransmission() == 0;
        if (!touchOk) logf("lcd: no hay táctil FT6336\n");
    #endif
        fb.attach(draw_, W, H);
        memset(shown_, 0, (size_t)W * H * 2);
        ok = true;
        lastActivity = millis();
        ui.splash(fb, ROTOR_VERSION);                    // la primera imagen es la de arranque (con el brillo a 0)
        splashOn = true;
        nextBand = 0;
        logf("lcd: %s %dx%d%s\n", LCD_PANEL == 3 ? "RGB" : LCD_PANEL == 2 ? "RM690B0" : "ST7796", W, H,
             touchOk ? (LCD_PANEL == 3 ? " + GT911" : " + FT6336") : "");
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
                if (splashOn) { splashTouched = true; swallow = true; }         // un toque quita la pantalla de arranque
                else if (dimmed) { dimmed = false; applyBright(); swallow = true; }   // el primer toque solo enciende
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
        if (!dimmed && !splashOn && Cfg.lcdDimS > 0 && now - lastActivity > (uint32_t)Cfg.lcdDimS * 1000u) { dimmed = true; applyBright(); }
        if (brightSaveAt && (int32_t)(now - brightSaveAt) >= 0) { brightSaveAt = 0; Config::save(); }

        // Mandar a la pantalla lo que cambió: unas pocas franjas por vuelta para no frenar el control del rotor
        if (nextBand >= 0) {
            pushSome();
            if (nextBand < 0 && firstFrame) {
                firstFrame = false;
                if (splashOn) splashT0 = now; else applyBright();
            }
            return;
        }

        // Pantalla de arranque: brillo de 0 al configurado en SPLASH_FADE_MS, luego SPLASH_HOLD_MS (o hasta un toque)
        if (splashOn) {
            const uint32_t t = now - splashT0;
            if (t >= SPLASH_FADE_MS + SPLASH_HOLD_MS || splashTouched) {
                splashOn = false; applyBright(); lastDraw = 0;
            } else {
                if (now - lastFadeMs >= 30) {
                    lastFadeMs = now;
                    const int pct = t >= SPLASH_FADE_MS ? Cfg.lcdBright : (int)(Cfg.lcdBright * t / SPLASH_FADE_MS);
                    if (pct != fadePct) { fadePct = pct; setBright(pct); }
                }
                return;
            }
        }

        // Dibujar 5 veces por segundo (o enseguida tras un toque)
        if (lastDraw && now - lastDraw < 200) return;
        lastDraw = now ? now : 1;
        fillState();
        ui.setTheme(Cfg.lcdTheme);                       // cambia en caliente: se manda lo que cambia
        ui.setFrameTime(now);
        ui.draw(fb, st);
        nextBand = 0;
    }
}
#endif
