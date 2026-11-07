/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "board.h"
#include <time.h>
#include <stdarg.h>
#include <math.h>
#if ROTOR_HAS_WIFI
    #include <WiFi.h>
#endif

namespace Board {

    void begin() {
    #if ROTOR_RP2
        analogReadResolution(12);
    #endif
    }

    const char* chip() {
    #if ROTOR_ESP32
        return ESP.getChipModel();
    #else
        #if defined(PICO_RP2350)
            return "RP2350";
        #else
            return "RP2040";
        #endif
    #endif
    }

    void reboot() {
        delay(200);
    #if ROTOR_ESP32
        ESP.restart();
    #else
        rp2040.reboot();
    #endif
    }

    double readAdc(int pin) {
        if (pin < 0) return NAN;
        uint32_t acc = 0;
        const int n = 8;
        for (int i = 0; i < n; i++) {
        #if ROTOR_ESP32
            acc += analogReadMilliVolts(pin);
        #else
            acc += analogRead(pin);
        #endif
        }
        return (double)acc / n;
    }

    void pwmAttach(int pin, int freq) {
        if (pin < 0) return;
    #if ROTOR_ESP32
        #if ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcAttach(pin, freq, 10);
        #else
            static int ch = 0;
            ledcSetup(ch, freq, 10);
            ledcAttachPin(pin, ch++);
        #endif
    #else
        analogWriteFreq(freq);
        analogWriteRange(1023);
        pinMode(pin, OUTPUT);
    #endif
        pwmWrite(pin, 0);
    }

    void pwmWrite(int pin, double duty) {
        if (pin < 0) return;
        if (duty < 0) duty = 0;
        if (duty > 1) duty = 1;
        const uint32_t v = (uint32_t)lround(duty * 1023.0);
    #if ROTOR_ESP32
        #if ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcWrite(pin, v);
        #else
            (void)v;   // con el API antiguo haria falta el canal: no se usa con la plataforma del proyecto
        #endif
    #else
        analogWrite(pin, v);
    #endif
    }

    void digitalOut(int pin, bool level) {
        if (pin < 0) return;
        digitalWrite(pin, level ? HIGH : LOW);
    }

    uint64_t utc() {
        const time_t t = time(nullptr);
        return t > 1600000000 ? (uint64_t)t : 0;
    }

    void startNtp(const char* server) {
    #if ROTOR_HAS_WIFI
        if (!server || !server[0]) return;
        #if ROTOR_ESP32
            configTime(0, 0, server);
        #else
            NTP.begin(server);
        #endif
    #else
        (void)server;
    #endif
    }

    uint32_t freeHeap() {
    #if ROTOR_ESP32
        return ESP.getFreeHeap();
    #else
        return rp2040.getFreeHeap();
    #endif
    }
}
