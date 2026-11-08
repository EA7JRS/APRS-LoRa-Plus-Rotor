/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Expansores de E/S en I2C de las placas de Waveshare con pantalla. En la configuración del rotor, EXIOn es el pin 100 + n
// y EXIO_ADC el pin 200 (ver docs/CONFIG.md).
//
// EXIO_CHIP 1: CH32V00x con su propio programa en 0x24 (ESP32-C5-Touch-LCD-3.5). Registros (ejemplos de Waveshare,
//   examples/arduino/examples/01_exio y 07_lvgl_demo): 0x02 dirección (1 = salida), 0x03 salidas, 0x04 entradas (16 bits, EXIO0-7
//   primero), 0x05 PWM de la retroiluminación (0-255), 0x06 ADC del pin EXIO_ADC (16 bits).
//   EXIO0 reset del táctil, EXIO1 reset del LCD, EXIO2 INT1 del QMI8658, EXIO3 IRQ del AXP2101, EXIO4 INT del RTC,
//   EXIO5 amplificador de audio, EXIO6 botón PWR; EXIO8-EXIO15 y EXIO_ADC salen por el conector de 32 pines.
// EXIO_CHIP 2: TCA9554 en 0x20 (ESP32-S3-Touch-AMOLED-2.41): 0x00 entradas, 0x01 salidas, 0x03 configuración (1 = entrada).
//   EXIO0 reset de la pantalla, EXIO1 reset del táctil, EXIO2 INT del RTC, EXIO3 / EXIO4 INT del IMU; EXIO5-EXIO7 en el conector.
//   Sin ADC ni PWM.
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
    void     backlight(uint8_t pct);        // 0-100 (solo CH32V)
    void     resetLcdTouch();               // pulso en EXIO0 / EXIO1 (reset de la pantalla y del táctil)
#endif
}
