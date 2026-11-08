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
#if EXIO_CHIP == 2
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
    static uint16_t out_ = (1u << 0) | (1u << 1);

#if EXIO_CHIP == 2
    static bool write8(uint8_t reg, uint8_t v) {
        Wire.beginTransmission(ADDR);
        Wire.write(reg); Wire.write(v);
        return Wire.endTransmission() == 0;
    }
    static bool writeOut() { return write8(R_OUT, (uint8_t)out_); }
    static bool writeDir() { return write8(R_CFG, (uint8_t)~dir_); }
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
    #if EXIO_CHIP == 2
        ok_ = writeOut() && writeDir();
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
    #if EXIO_CHIP == 2
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
    #if EXIO_CHIP == 2
        const bool w = writeOut();
    #else
        const bool w = write16(R_OUT, out_);
    #endif
        if (!w) logf("exio: error al escribir\n");
    }

    bool read(int n) {
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
    #else
        if (pct > 100) pct = 100;
        Wire.beginTransmission(ADDR);
        Wire.write(R_PWM); Wire.write((uint8_t)(pct * 255u / 100u));
        Wire.endTransmission();
    #endif
    }

    void resetLcdTouch() {
        write(0, false); write(1, false);
        delay(20);
        write(0, true); write(1, true);
        delay(120);
    }
}
#endif
