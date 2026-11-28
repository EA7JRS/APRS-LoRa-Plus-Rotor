/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */
#pragma once
// Configuracion del static hold de un receptor u-blox conectado a un conector de la placa (UART), igual que el tracker
// APRS LoRa+ (gps_utils.cpp) y el demonio de la Raspberry (gnss.py): el receptor congela la posicion con velocidad baja.
// "m8" (u-blox 8) por UBX-CFG-NAV5 (interfaz antigua por mensajes); "m10" (MAX-M10S) y "x20" (ZED-X20P) por UBX-CFG-VALSET
// (base de datos de configuracion, claves CFG-MOT). Solo arma las tramas (bytes); el firmware las escribe por el UART del GPS.
#include <stdint.h>
#include <string>

namespace RC {

    // Enmarca un mensaje UBX: B5 62 + clase + id + longitud (LE) + payload + suma de Fletcher de 8 bits
    inline std::string ubxFrame(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len) {
        std::string f;
        f.push_back((char)0xB5); f.push_back((char)0x62);
        uint8_t ck_a = 0, ck_b = 0;
        auto add = [&](uint8_t b) { f.push_back((char)b); ck_a = (uint8_t)(ck_a + b); ck_b = (uint8_t)(ck_b + ck_a); };
        add(cls); add(id); add((uint8_t)(len & 0xFF)); add((uint8_t)(len >> 8));
        for (uint16_t i = 0; i < len; i++) add(payload[i]);
        f.push_back((char)ck_a); f.push_back((char)ck_b);
        return f;
    }

    // UBX-CFG-VALSET (0x06 0x8A) en capas RAM + BBR con el static hold (CFG-MOT), para u-blox M10 / X20.
    // cms = CFG-MOT-GNSSSPEED_THRS (cm/s), distM = CFG-MOT-GNSSDIST_THRS (m).
    inline std::string ubxValsetHold(int cms, int distM) {
        uint8_t p[4 + 4 + 1 + 4 + 2];
        int n = 0;
        p[n++] = 0x00; p[n++] = 0x03; p[n++] = 0x00; p[n++] = 0x00;          // version 0, capas RAM (bit0) + BBR (bit1)
        const uint32_t kSpeed = 0x20250038;                                  // tamanio 1 byte (bits 30:28 = 2)
        for (int b = 0; b < 4; b++) p[n++] = (uint8_t)(kSpeed >> (8 * b));
        p[n++] = (uint8_t)(cms < 0 ? 0 : cms > 255 ? 255 : cms);
        const uint32_t kDist = 0x3025003B;                                   // tamanio 2 bytes (bits 30:28 = 3)
        for (int b = 0; b < 4; b++) p[n++] = (uint8_t)(kDist >> (8 * b));
        const int d = distM < 0 ? 0 : distM > 65535 ? 65535 : distM;
        p[n++] = (uint8_t)(d & 0xFF); p[n++] = (uint8_t)(d >> 8);
        return ubxFrame(0x06, 0x8A, p, (uint16_t)n);
    }

    // UBX-CFG-NAV5 (0x06 0x24) con solo el static hold (mask bit 6), para u-blox 8 (interfaz antigua por mensajes).
    inline std::string ubxCfgNav5Hold(int cms, int distM) {
        uint8_t p[36] = {0};
        p[0] = 0x40; p[1] = 0x00;                                            // mask = 0x0040: solo static hold
        p[22] = (uint8_t)(cms < 0 ? 0 : cms > 255 ? 255 : cms);              // staticHoldThresh (cm/s)
        const int d = distM < 0 ? 0 : distM > 65535 ? 65535 : distM;
        p[28] = (uint8_t)(d & 0xFF); p[29] = (uint8_t)(d >> 8);              // staticHoldMaxDist (m)
        return ubxFrame(0x06, 0x24, p, 36);
    }

    // Trama de static hold segun el receptor ("m8" / "m10" / "x20"); vacia si no es un u-blox conocido o cms <= 0
    inline std::string ubxStaticHold(const std::string& chip, int cms, int distM) {
        if (cms <= 0) return "";
        if (chip == "m10" || chip == "x20") return ubxValsetHold(cms, distM);
        if (chip == "m8") return ubxCfgNav5Hold(cms, distM);
        return "";
    }
}
