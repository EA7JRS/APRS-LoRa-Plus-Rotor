/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Pantalla táctil de la Waveshare ESP32-C5-Touch-LCD-3.5: ST7796 (SPI, 320 x 480) y FT6336 (I2C).
// Se dibuja en memoria (PSRAM) y solo se mandan las franjas que cambian, sin bloquear el bucle del rotor.
#include "board.h"

namespace LcdHw {
#if ROTOR_HAS_LCD
    void begin();
    void loop();
#else
    inline void begin() {}
    inline void loop() {}
#endif
}
