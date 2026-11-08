/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "exio.h"
#if ROTOR_HAS_EXIO
#include <Wire.h>
#include "board.h"

#ifndef EXIO_SDA
    #define EXIO_SDA 27
#endif
#ifndef EXIO_SCL
    #define EXIO_SCL 26
#endif
#ifndef EXIO_CHIP
    #define EXIO_CHIP 1
#endif

namespace Exio {
#ifndef EXIO_USB_SEL_LEVEL
    #define EXIO_USB_SEL_LEVEL 0                // ESP32-S3-Touch-LCD-7B: EXIO5 bajo = USB nativo (alto = CAN)
#endif

#if EXIO_CHIP == 3
    static const uint8_t ADDR = 0x24;           // CH32V003 de 8 pines
    static const uint8_t R_DIR = 0x02, R_OUT = 0x03, R_IN = 0x04, R_PWM = 0x05, R_ADC = 0x06;
    static const uint16_t BOARD_OUT = 0x7E;     // EXIO1-EXIO6
    static const int TP_RST = 1, LCD_RST = 3, DISP = 2;
#elif EXIO_CHIP == 2
    static const uint8_t ADDR = 0x20;           // TCA9554
    static const uint8_t R_IN = 0x00, R_OUT = 0x01, R_CFG = 0x03;
    static const uint16_t BOARD_OUT = (1u << 0) | (1u << 1);
#else
    static const uint8_t ADDR = 0x24;           // CH32V00x
    static const uint8_t R_DIR = 0x02, R_OUT = 0x03, R_IN = 0x04, R_PWM = 0x05, R_ADC = 0x06;
    // Salidas fijas de la placa: EXIO0 (reset táctil), EXIO1 (reset LCD), EXIO5 (amplificador de audio, apagado)
    static const uint16_t BOARD_OUT = (1u << 0) | (1u << 1) | (1u << 5);
#endif
    static bool     ok_ = false;
    static uint16_t dir_ = BOARD_OUT;
#if EXIO_CHIP == 3
    // resets sueltos, luz de fondo apagada hasta el primer dibujo, TF sin elegir, USB, LCD alimentado
    static uint16_t out_ = (1u << 1) | (1u << 3) | (1u << 4) | (EXIO_USB_SEL_LEVEL ? (1u << 5) : 0) | (1u << 6);
#else
    static uint16_t out_ = (1u << 0) | (1u << 1);
#endif

#if EXIO_CHIP == 2 || EXIO_CHIP == 3
    static bool write8(uint8_t reg, uint8_t v) {
        Wire.beginTransmission(ADDR);
        Wire.write(reg); Wire.write(v);
        return Wire.endTransmission() == 0;
    }
    static bool writeOut() { return write8(R_OUT, (uint8_t)out_); }
  #if EXIO_CHIP == 3
    static bool writeDir() { return write8(R_DIR, (uint8_t)dir_); }
  #else
    static bool writeDir() { return write8(R_CFG, (uint8_t)~dir_); }
  #endif
#endif
    // El programa de 16 pines espera EXIO0-7 y luego EXIO8-15; con el de 8 pines el segundo byte va al registro siguiente,
    // que se reescribe justo después (dirección -> salidas)
    [[maybe_unused]] static bool write16(uint8_t reg, uint16_t v) {
        Wire.beginTransmission(ADDR);
        Wire.write(reg); Wire.write((uint8_t)v); Wire.write((uint8_t)(v >> 8));
        return Wire.endTransmission() == 0;
    }
    static bool read16(uint8_t reg, uint16_t& v) {
        Wire.beginTransmission(ADDR);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom(ADDR, (uint8_t)2) != 2) return false;
        const uint8_t lo = Wire.read(), hi = Wire.read();
        v = (uint16_t)(lo | (hi << 8));
        return true;
    }

    bool begin() {
        Wire.begin(EXIO_SDA, EXIO_SCL);
        Wire.setClock(400000);
        Wire.beginTransmission(ADDR);
        ok_ = Wire.endTransmission() == 0;
        if (!ok_) { logf("exio: no hay expansor en 0x%02X\n", ADDR); return false; }
    #if EXIO_CHIP == 2 || EXIO_CHIP == 3
        ok_ = writeOut() && writeDir() && writeOut();
    #else
        ok_ = write16(R_DIR, dir_) && write16(R_OUT, out_);
    #endif
        return ok_;
    }

    bool ok() { return ok_; }

    void output(int n) {
        if (n < 0 || n > 15) return;
        const uint16_t d = dir_ | (uint16_t)(1u << n);
        if (d == dir_) return;
        dir_ = d;
    #if EXIO_CHIP == 2 || EXIO_CHIP == 3
        writeOut(); writeDir();
    #else
        write16(R_OUT, out_);                   // nivel primero, luego la dirección
        write16(R_DIR, dir_);
        write16(R_OUT, out_);
    #endif
    }

    void write(int n, bool level) {
        if (n < 0 || n > 15) return;
        const uint16_t o = level ? (out_ | (uint16_t)(1u << n)) : (out_ & (uint16_t)~(1u << n));
        if (o == out_) return;
        out_ = o;
    #if EXIO_CHIP == 2 || EXIO_CHIP == 3
        const bool w = writeOut();
    #else
        const bool w = write16(R_OUT, out_);
    #endif
        if (!w) logf("exio: error al escribir\n");
    }

    bool read(int n) {
    #if EXIO_CHIP == 3
        if (n < 0 || n > 7) return false;
        Wire.beginTransmission(ADDR);
        Wire.write(R_IN);
        if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDR, (uint8_t)1) != 1) return false;
        return (Wire.read() >> n) & 1;
    #endif
        uint16_t v = 0;
        if (n < 0 || n > 15 || !read16(R_IN, v)) return false;   // el TCA9554 devuelve 1 byte útil
        return (v >> n) & 1;
    }

    int adc() {
    #if EXIO_CHIP == 2
        return -1;
    #else
        uint16_t v = 0;
        return read16(R_ADC, v) ? (int)v : -1;
    #endif
    }

    void backlight(uint8_t pct) {
    #if EXIO_CHIP == 2
        (void)pct;
    #elif EXIO_CHIP == 3
        // EXIO2 enciende la luz; el PWM va al revés (0 = todo). 0 % = apagada
        if (pct > 100) pct = 100;
        write(DISP, pct > 0);
        if (pct > 0) {
            uint32_t duty = 100u - pct;
            if (duty > 97) duty = 97;
            write8(R_PWM, (uint8_t)(duty * 255u / 100u));
        }
    #else
        if (pct > 100) pct = 100;
        Wire.beginTransmission(ADDR);
        Wire.write(R_PWM); Wire.write((uint8_t)(pct * 255u / 100u));
        Wire.endTransmission();
    #endif
    }

    void resetLcdTouch() {
    #if EXIO_CHIP == 3
        write(TP_RST, false); write(LCD_RST, false);
        delay(20);
        write(LCD_RST, true); write(TP_RST, true);
        delay(120);
    #else
        write(0, false); write(1, false);
        delay(20);
        write(0, true); write(1, true);
        delay(120);
    #endif
    }
}
#endif
