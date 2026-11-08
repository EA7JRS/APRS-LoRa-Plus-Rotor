/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "cat.h"
#include "config.h"
#include "board.h"
#include "drivers.h"
#if ROTOR_HAS_WIFI
    #include <WiFi.h>
#endif

namespace Cat {
    static RC::CatEngine*  eng = nullptr;
    static RC::CatTracker  trk;
    static Stream*         port = nullptr;
    static uint32_t        stepHz = 1000;
#if ROTOR_HAS_WIFI
    static WiFiClient      tcp;
    static bool            tcpUp = false;
    static uint32_t        nextTry = 0;
#endif

    RC::CatEngine*  engine() { return eng; }
    RC::CatTracker& tracker() { return trk; }
    uint32_t        step() { return stepHz; }
    void            setStep(uint32_t hz) { if (hz >= 1 && hz <= 1000000) stepHz = hz; }

    bool tune(int steps) {
        if (!eng || !eng->state().link || !eng->state().freq) return false;
        const int64_t f = (int64_t)eng->state().freq, s = (int64_t)stepHz;
        int64_t n = (f / s + steps) * s;                      // queda en múltiplo del paso
        if (steps < 0 && f % s) n = (f / s + steps + 1) * s;
        if (n <= 0) return false;
        return eng->setFreq((uint64_t)n);
    }

    static void openUart() {
    #if ROTOR_ESP32
        // ESP32 / ESP32-S3: UART1 (la pasarela del rotor va en la UART2). ESP32-C5 / C6: solo hay dos UART -> UART0
        #if defined(SOC_UART_HP_NUM) && SOC_UART_HP_NUM < 3
            HardwareSerial& u = Serial0;
        #else
            HardwareSerial& u = Serial1;
        #endif
        u.begin(Cfg.catBaud, SERIAL_8N1, Cfg.catRx, Cfg.catTx);
        port = &u;
    #else
        // Pico: la UART que admita esos pines y que no use la pasarela del rotor (RX de la UART0: GP1, GP13, GP17, GP29)
        auto onUart0 = [](int rx) { return rx == 1 || rx == 13 || rx == 17 || rx == 29; };
        const bool gw0 = Cfg.driver == 1 && onUart0(Cfg.serRx), gw1 = Cfg.driver == 1 && !gw0;
        if (onUart0(Cfg.catRx) && !gw0 && Serial1.setRX(Cfg.catRx) && Serial1.setTX(Cfg.catTx)) { Serial1.begin(Cfg.catBaud); port = &Serial1; }
        else if (!onUart0(Cfg.catRx) && !gw1 && Serial2.setRX(Cfg.catRx) && Serial2.setTX(Cfg.catTx)) { Serial2.begin(Cfg.catBaud); port = &Serial2; }
        else logf("cat: pines RX %d / TX %d sin UART libre\n", Cfg.catRx, Cfg.catTx);
    #endif
    }

    void begin() {
        if (!Cfg.catProto) return;
        eng = RC::makeCat(Cfg.catProto, (uint8_t)Cfg.catCiv);
        if (!eng) return;
        eng->allowPtt = Cfg.catPtt;
        eng->pollGapMs = Cfg.catPollMs;
        trk.minStep = (uint32_t)Cfg.catTrackStep;
        char b[96];
        const bool net = RC::catIsNetwork(Cfg.catProto);
        const int defPort = Cfg.catProto == RC::CAT_SDRCONNECT ? 5454 : 4532;
        const int p = Cfg.catPort ? Cfg.catPort : defPort;
        if (Cfg.catProto == RC::CAT_SDRCONNECT) { auto* s = (RC::SdrConnectEngine*)eng; s->host = Cfg.catHost; s->port = p; }
        if (Cfg.catProto == RC::CAT_RIGCTL) { auto* r = (RC::RigctlClientEngine*)eng; r->host = Cfg.catHost; r->port = p; }
        if (net) snprintf(b, sizeof b, "%s · %s %s:%d", Cfg.catModel.empty() ? "SDR" : Cfg.catModel.c_str(), RC::catProtoName(Cfg.catProto), Cfg.catHost.c_str(), p);
        else if (Cfg.catProto == RC::CAT_CIV) snprintf(b, sizeof b, "%s · CI-V %02X · %d", Cfg.catModel.empty() ? "Icom" : Cfg.catModel.c_str(), Cfg.catCiv, Cfg.catBaud);
        else snprintf(b, sizeof b, "%s · %s · %d", Cfg.catModel.empty() ? RC::catProtoName(Cfg.catProto) : Cfg.catModel.c_str(), RC::catProtoName(Cfg.catProto), Cfg.catBaud);
        eng->name = b;
        if (Cfg.catProto == RC::CAT_YAESU || Cfg.catProto == RC::CAT_KENWOOD || Cfg.catProto == RC::CAT_RIGCTL) stepHz = 100;
        if (!net) openUart();
        logf("cat: %s\n", b);
    }

    static uint32_t pttSince = 0;

    void loop() {
        if (!eng) return;
        const uint32_t now = millis();
        // Límite de emisión continua (por si se pierde la orden de soltar el PTT)
        if (eng->state().ptt) {
            if (!pttSince) pttSince = now ? now : 1;
            else if (now - pttSince > (uint32_t)Cfg.catPttMaxS * 1000u) { eng->setPtt(false); pttSince = 0; logf("cat: PTT soltado por tiempo\n"); }
        } else pttSince = 0;
        std::string out;
        uint8_t buf[256];
        if (RC::catIsNetwork(Cfg.catProto)) {
        #if ROTOR_HAS_WIFI
            const bool wifi = WiFi.status() == WL_CONNECTED;
            if (tcpUp && (!tcp.connected() || !wifi)) { tcp.stop(); tcpUp = false; eng->onDisconnect(); nextTry = now + 3000; }
            if (Cfg.catProto == RC::CAT_SDRCONNECT && tcpUp && ((RC::SdrConnectEngine*)eng)->stale(now)) {
                tcp.stop(); tcpUp = false; eng->onDisconnect(); nextTry = now + 2000;
            }
            // Conectar puede bloquear un momento si el equipo no está: nunca con el rotor moviéndose
            const bool moving = (Drv::rotor().status() & (RC::ST_MOVING | RC::ST_BUSY)) != 0;
            if (!tcpUp && wifi && !moving && (int32_t)(now - nextTry) >= 0) {
                const int p = Cfg.catPort ? Cfg.catPort : (Cfg.catProto == RC::CAT_SDRCONNECT ? 5454 : 4532);
            #if ROTOR_ESP32
                const bool c = tcp.connect(Cfg.catHost.c_str(), p, 400);
            #else
                tcp.setTimeout(400);
                const bool c = tcp.connect(Cfg.catHost.c_str(), p);
            #endif
                if (c) { tcp.setNoDelay(true); tcpUp = true; eng->onConnect(millis()); }
                else nextTry = millis() + 5000;           // no insistir a cada vuelta
            }
            if (!tcpUp) return;
            int n;
            while ((n = tcp.available()) > 0) {
                n = tcp.read(buf, n > (int)sizeof(buf) ? (int)sizeof(buf) : n);
                if (n <= 0) break;
                eng->feed(buf, (size_t)n, now);
            }
            eng->tick(now, out);
            trk.step(*eng, now);
            if (!out.empty()) tcp.write((const uint8_t*)out.data(), out.size());
        #endif
            return;
        }
        if (!port) return;
        int k = 0;
        while (port->available() && k < (int)sizeof(buf)) { const int c = port->read(); if (c < 0) break; buf[k++] = (uint8_t)c; }
        if (k) eng->feed(buf, (size_t)k, now);
        eng->tick(now, out);
        trk.step(*eng, now);
        if (!out.empty()) port->write((const uint8_t*)out.data(), out.size());
    }
}
