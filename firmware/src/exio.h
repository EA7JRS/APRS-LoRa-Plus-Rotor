/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Expansor de E/S de la Waveshare ESP32-C5-Touch-LCD-3.5: microcontrolador CH32V00x en I2C (0x24) con su propio programa.
// Registros (ejemplos de Waveshare, examples/arduino/examples/01_exio y 07_lvgl_demo):
//   0x02 dirección (1 = salida), 0x03 salidas, 0x04 entradas (16 bits, EXIO0-7 primero), 0x05 PWM de la retroiluminación (0-255),
//   0x06 ADC del pin EXIO_ADC (16 bits).
// EXIO0 reset del táctil, EXIO1 reset del LCD, EXIO2 INT1 del QMI8658, EXIO3 IRQ del AXP2101, EXIO4 INT del RTC,
// EXIO5 amplificador de audio, EXIO6 botón PWR; EXIO8-EXIO15 y EXIO_ADC salen por el conector de 32 pines.
// En la configuración del rotor, EXIOn es el pin 100 + n y EXIO_ADC el pin 200 (ver docs/CONFIG.md).
#include <Arduino.h>

#ifndef ROTOR_HAS_EXIO
    #define ROTOR_HAS_EXIO 0
#endif

#define PIN_EXIO_BASE   100
#define PIN_EXIO_ADC    200

namespace Exio {
    inline bool isExio(int pin) { return pin >= PIN_EXIO_BASE && pin < PIN_EXIO_BASE + 16; }
    inline bool isAdc(int pin)  { return pin == PIN_EXIO_ADC; }
#if ROTOR_HAS_EXIO
    bool     begin();                       // I2C del expansor (y del táctil, RTC, IMU, PMU): EXIO_SDA / EXIO_SCL
    bool     ok();
    void     output(int n);                 // EXIOn como salida
    void     write(int n, bool level);      // solo escribe si cambia
    bool     read(int n);
    int      adc();                         // -1 = error
    void     backlight(uint8_t pct);        // 0-100
    void     resetLcdTouch();               // pulso en EXIO0 / EXIO1
#endif
}
