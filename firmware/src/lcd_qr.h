/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// QR del donativo (https://paypal.me/ea7jrs), como en el «Acerca de» de APRS LoRa+ Display: versión 2, corrección M,
// 25 x 25 módulos; fila y = LCD_QR[y], módulo x = bit x (1 = oscuro). Generado una vez con qrcodegen de Project Nayuki
// (MIT), el mismo que usa LVGL en la Display. Se dibuja con 2 módulos de margen claro alrededor.
#include <stdint.h>

namespace Lcd {
    static const char LCD_QR_TEXT[] = "https://paypal.me/ea7jrs";
    static const int LCD_QR_N = 25;
    static const uint32_t LCD_QR[25] = {
        0x01FC3C7F,
        0x0105F641,
        0x0175D15D,
        0x0174ED5D,
        0x01751D5D,
        0x0105C541,
        0x01FD557F,
        0x0001CF00,
        0x007C707D,
        0x00890434,
        0x01A498EA,
        0x01074123,
        0x01DE28C1,
        0x00AA569B,
        0x01B8E2F1,
        0x011FD385,
        0x005F76C5,
        0x0031A500,
        0x01D5387F,
        0x01310941,
        0x005FF95D,
        0x01F6575D,
        0x0162A15D,
        0x013D9C41,
        0x01FC677F
    };
}
