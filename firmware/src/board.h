/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Capa minima entre placas: ESP32 / ESP32-S3 (arduino-esp32) y RP2350 Pico 2 / 2 W (arduino-pico de Earle Philhower)
#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
    #define ROTOR_ESP32 1
    #ifndef ROTOR_HAS_WIFI
        #define ROTOR_HAS_WIFI 1
    #endif
    #if defined(ROTOR_HAS_SPP) && !defined(CONFIG_IDF_TARGET_ESP32)
        #undef ROTOR_HAS_SPP          // solo el ESP32 original tiene Bluetooth clasico
    #endif
#elif defined(ARDUINO_ARCH_RP2040)
    #define ROTOR_RP2 1
    // Las cabeceras de WiFi de arduino-pico ocultan sobrecargas virtuales (aviso que -Werror convierte en error)
    #pragma GCC diagnostic ignored "-Woverloaded-virtual"
#else
    #error "Placa no soportada"
#endif

#ifndef ROTOR_HAS_WIFI
    #define ROTOR_HAS_WIFI 0
#endif
#ifndef ROTOR_HAS_BLE
    #define ROTOR_HAS_BLE 0
#endif
#ifndef ROTOR_HAS_SPP
    #define ROTOR_HAS_SPP 0
#endif

// Pines por defecto (los pone cada entorno de platformio.ini)
#ifndef DEF_AZ_CW
    #define DEF_AZ_CW -1
#endif
#ifndef DEF_AZ_CCW
    #define DEF_AZ_CCW -1
#endif
#ifndef DEF_EL_UP
    #define DEF_EL_UP -1
#endif
#ifndef DEF_EL_DOWN
    #define DEF_EL_DOWN -1
#endif
#ifndef DEF_AZ_ADC
    #define DEF_AZ_ADC -1
#endif
#ifndef DEF_EL_ADC
    #define DEF_EL_ADC -1
#endif
#ifndef DEF_I2C_SDA
    #define DEF_I2C_SDA -1
#endif
#ifndef DEF_I2C_SCL
    #define DEF_I2C_SCL -1
#endif
#ifndef DEF_SER_RX
    #define DEF_SER_RX -1
#endif
#ifndef DEF_SER_TX
    #define DEF_SER_TX -1
#endif
#ifndef DEF_LED
    #define DEF_LED -1
#endif
#ifndef DEF_LORA_CHIP
    #define DEF_LORA_CHIP 0
#endif
#ifndef DEF_LORA_SCK
    #define DEF_LORA_SCK -1
#endif
#ifndef DEF_LORA_MISO
    #define DEF_LORA_MISO -1
#endif
#ifndef DEF_LORA_MOSI
    #define DEF_LORA_MOSI -1
#endif
#ifndef DEF_LORA_CS
    #define DEF_LORA_CS -1
#endif
#ifndef DEF_LORA_RST
    #define DEF_LORA_RST -1
#endif
#ifndef DEF_LORA_IRQ
    #define DEF_LORA_IRQ -1
#endif
#ifndef DEF_LORA_BUSY
    #define DEF_LORA_BUSY -1
#endif
#ifndef DEF_LORA_TCXO
    #define DEF_LORA_TCXO 0.0
#endif

namespace Board {
    void        begin();
    const char* chip();                         // "ESP32", "ESP32-S3", "RP2350"...
    void        reboot();
    double      readAdc(int pin);               // ESP32: milivoltios calibrados; RP2350: cuentas de 12 bits. NAN si el pin no vale
    void        pwmAttach(int pin, int freq);
    void        pwmWrite(int pin, double duty); // 0-1
    void        digitalOut(int pin, bool level);
    uint64_t    utc();                          // hora UTC en segundos (0 = desconocida)
    void        startNtp(const char* server);
    uint32_t    freeHeap();
}

// Registro: solo sale por USB si el USB esta en modo registro
void logf(const char* fmt, ...);
