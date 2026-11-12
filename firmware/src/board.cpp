/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "board.h"
#if ROTOR_ESP32 && ROTOR_HAS_WIFI
    #include "tsync.h"
#endif
#include "exio.h"
#include "config.h"
#include <Wire.h>
#include <time.h>
#include <stdarg.h>
#include <math.h>
#if ROTOR_HAS_WIFI
    #include <WiFi.h>
#endif

namespace Board {

    // PCF8574 de una placa de relés en el I2C de la configuración (direct.i2cSda / i2cScl, direct.pcfAddr): pines 120-127.
    // Escribir 1 deja el pin arriba con su resistencia débil y 0 lo lleva a masa: las placas de relés suelen activar con 0
    // (direct.activeHigh: false).
    static inline bool isPcf(int pin) { return pin >= PIN_PCF_BASE && pin < PIN_PCF_BASE + 8; }
    static uint8_t pcfOut = 0xFF;
    static bool    pcfStarted = false;
    bool i2cBegin() {
        static int state = 0;                   // 0 sin empezar, 1 en marcha, -1 sin pines
        if (state) return state > 0;
    #if ROTOR_HAS_EXIO
        state = Exio::ok() ? 1 : -1;            // el expansor ya arrancó el I2C (los mismos pines)
    #else
        if (Cfg.i2cSda < 0 || Cfg.i2cScl < 0) { state = -1; logf("i2c: sin pines (direct.i2cSda / i2cScl)\n"); return false; }
      #if ROTOR_ESP32
        Wire.begin(Cfg.i2cSda, Cfg.i2cScl);
      #else
        Wire.setSDA(Cfg.i2cSda); Wire.setSCL(Cfg.i2cScl); Wire.begin();
      #endif
        Wire.setClock(400000);
        state = 1;
    #endif
        return state > 0;
    }

    static void pcfWrite() {
        pcfStarted = true;
        if (!i2cBegin()) return;
        Wire.beginTransmission((uint8_t)Cfg.pcfAddr);
        Wire.write(pcfOut);
        if (Wire.endTransmission() != 0) logf("pcf8574: no responde en 0x%02X\n", Cfg.pcfAddr);
    }

    void begin() {
    #ifdef BOARD_POWER_HOLD
        pinMode(BOARD_POWER_HOLD, OUTPUT);     // con batería, la placa sigue encendida mientras este pin esté arriba
        digitalWrite(BOARD_POWER_HOLD, HIGH);
    #endif
    #if ROTOR_RP2
        analogReadResolution(12);
    #endif
    #if ROTOR_HAS_EXIO
        Exio::begin();                  // antes que nada: las salidas del rotor pueden estar en el expansor
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
        if (Exio::isAdc(pin) || Exio::isExio(pin)) {
        #if ROTOR_HAS_EXIO
            if (!Exio::isAdc(pin)) return NAN;
            const int a = Exio::adc(), b = Exio::adc();
            if (a < 0 || b < 0) return NAN;
            return (a + b) / 2.0;
        #else
            return NAN;
        #endif
        }
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

    bool adcIsCounts(int pin) {
    #if ROTOR_ESP32
        return Exio::isAdc(pin);
    #else
        (void)pin;
        return true;
    #endif
    }

    void pinOutput(int pin) {
        if (pin < 0) return;
        if (isPcf(pin)) { pcfWrite(); return; }
        if (Exio::isExio(pin)) {
        #if ROTOR_HAS_EXIO
            Exio::output(pin - PIN_EXIO_BASE);
        #endif
            return;
        }
        if (pin >= PIN_EXIO_BASE) return;
        pinMode(pin, OUTPUT);
    }

    void pwmAttach(int pin, int freq) {
        if (pin < 0) return;
        if (pin >= PIN_EXIO_BASE) { logf("pin %d: sin PWM (expansor); usa reles\n", pin); return; }
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
        if (pin < 0 || pin >= PIN_EXIO_BASE) return;
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
        if (isPcf(pin)) {
            const uint8_t bit = (uint8_t)(1u << (pin - PIN_PCF_BASE));
            const uint8_t o = level ? (uint8_t)(pcfOut | bit) : (uint8_t)(pcfOut & ~bit);
            if (o != pcfOut || !pcfStarted) { pcfOut = o; pcfWrite(); }
            return;
        }
        if (pin >= PIN_EXIO_BASE) {
        #if ROTOR_HAS_EXIO
            if (Exio::isExio(pin)) Exio::write(pin - PIN_EXIO_BASE, level);
        #endif
            return;
        }
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
            tsync::startNtp(server);            // lib/aprs_links: el servidor, hora.roa.es y pool.ntp.org; el GPS de la red manda
        #else
            NTP.begin(server, "pool.ntp.org");
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
