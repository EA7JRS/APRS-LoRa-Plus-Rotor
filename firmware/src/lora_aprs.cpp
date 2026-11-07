/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "lora_aprs.h"
#include <SPI.h>
#include <RadioLib.h>
#include <deque>
#include "config.h"
#include "board.h"
#include "drivers.h"
#include "rc_aprs.h"

namespace LoraAprs {

    static Module*        mod = nullptr;
    static PhysicalLayer* phy = nullptr;
    static bool           ok = false;
    static volatile bool  irq = false;
    static bool           transmitting = false;
    static std::deque<std::string> txq;
    static RC::AprsControl ctl;
    static std::string    last;
    static uint32_t       lastBeacon = 0, txDoneAt = 0, rxAt = 0;
    static float          rssi = 0;

#if defined(ARDUINO_ARCH_ESP32)
    static void IRAM_ATTR onIrq() { irq = true; }
#else
    static void onIrq() { irq = true; }
#endif

    bool active() { return ok; }
    std::string lastResult() { return last; }
    float lastRssi() { return rssi; }

    void send(const std::string& tnc2) {
        if (!ok || txq.size() >= 6) return;
        txq.push_back(tnc2);
    }

    static SPIClass* spiFor() {
    #if ROTOR_ESP32
        SPI.begin(Cfg.loraSck, Cfg.loraMiso, Cfg.loraMosi, Cfg.loraCs);
        return &SPI;
    #else
        // SPI0 o SPI1 segun los pines (el HAT de Waveshare usa SPI1: 10 / 11 / 12)
        if (SPI1.setSCK(Cfg.loraSck) && SPI1.setTX(Cfg.loraMosi) && SPI1.setRX(Cfg.loraMiso)) { SPI1.begin(); return &SPI1; }
        if (SPI.setSCK(Cfg.loraSck) && SPI.setTX(Cfg.loraMosi) && SPI.setRX(Cfg.loraMiso)) { SPI.begin(); return &SPI; }
        return nullptr;
    #endif
    }

    void begin() {
        ctl.cfg = Cfg.aprs;
        if (Cfg.loraChip == 0 || Cfg.loraCs < 0 || Cfg.loraIrq < 0) return;
        SPIClass* spi = spiFor();
        if (!spi) { last = "LoRa: SPI pins"; return; }
        mod = new Module(Cfg.loraCs, Cfg.loraIrq, Cfg.loraRst, Cfg.loraBusy < 0 ? RADIOLIB_NC : Cfg.loraBusy, *spi);
        int st = RADIOLIB_ERR_CHIP_NOT_FOUND;
        const uint8_t sync = 0x12;     // LoRa APRS (red privada)
        switch (Cfg.loraChip) {
        #if !defined(RADIOLIB_EXCLUDE_SX126X)
            case 1: case 2: {
                SX126x* r = (Cfg.loraChip == 1) ? (SX126x*)new SX1262(mod) : (SX126x*)new SX1268(mod);
                if (Cfg.loraChip == 1) st = ((SX1262*)r)->begin(Cfg.loraFreq, Cfg.loraBw, Cfg.loraSf, Cfg.loraCr, sync, Cfg.loraPower, 8, Cfg.loraTcxo);
                else                   st = ((SX1268*)r)->begin(Cfg.loraFreq, Cfg.loraBw, Cfg.loraSf, Cfg.loraCr, sync, Cfg.loraPower, 8, Cfg.loraTcxo);
                if (st == RADIOLIB_ERR_NONE) {
                    r->setDio2AsRfSwitch(true);
                    r->setPacketReceivedAction(onIrq);
                    phy = r;
                }
                break;
            }
        #endif
        #if !defined(RADIOLIB_EXCLUDE_SX127X)
            case 3: case 4: {
                SX127x* r = (Cfg.loraChip == 3) ? (SX127x*)new SX1276(mod) : (SX127x*)new SX1278(mod);
                if (Cfg.loraChip == 3) st = ((SX1276*)r)->begin(Cfg.loraFreq, Cfg.loraBw, Cfg.loraSf, Cfg.loraCr, sync, Cfg.loraPower, 8, 0);
                else                   st = ((SX1278*)r)->begin(Cfg.loraFreq, Cfg.loraBw, Cfg.loraSf, Cfg.loraCr, sync, Cfg.loraPower, 8, 0);
                if (st == RADIOLIB_ERR_NONE) {
                    r->setPacketReceivedAction(onIrq);
                    phy = r;
                }
                break;
            }
        #endif
            default: break;
        }
        if (!phy) { char b[48]; snprintf(b, sizeof(b), "LoRa: init error %d", st); last = b; logf("%s\n", b); return; }
        ok = true;
        phy->startReceive();
        logf("LoRa %.3f MHz SF%d listo\n", Cfg.loraFreq, Cfg.loraSf);
    }

    static void startRx() { irq = false; transmitting = false; phy->startReceive(); }

    static std::string beaconFrame() {
        RC::Rotor& r = Drv::rotor();
        const std::string st = RC::AprsControl::statusLine(r, Drv::fault() ? RC::faultText(Drv::fault()) : "OK");
        std::string f = Cfg.aprs.callsign + ">" + Cfg.aprs.tocall;
        if (!Cfg.aprs.path.empty()) f += "," + Cfg.aprs.path;
        if (Cfg.lat != 0 || Cfg.lon != 0) {
            char p[40];
            const double la = fabs(Cfg.lat), lo = fabs(Cfg.lon);
            snprintf(p, sizeof(p), ":!%02d%05.2f%c/%03d%05.2f%cr", (int)la, (la - (int)la) * 60.0, Cfg.lat >= 0 ? 'N' : 'S',
                     (int)lo, (lo - (int)lo) * 60.0, Cfg.lon >= 0 ? 'E' : 'W');
            f += p;
            f += st + " " + Cfg.comment;
        } else {
            f += ":>" + st + " " + Cfg.comment;
        }
        return f;
    }

    void loop() {
        if (!ok) return;
        const uint32_t now = millis();
        if (irq) {
            irq = false;
            if (transmitting) {
                phy->finishTransmit();
                txDoneAt = now;
                startRx();
            } else {
                uint8_t buf[256];
                const size_t len = phy->getPacketLength();
                const int st = phy->readData(buf, len > sizeof(buf) ? sizeof(buf) : len);
                rssi = phy->getRSSI();
                rxAt = now;
                if (st == RADIOLIB_ERR_NONE && len > 0) {
                    std::string frame((const char*)buf, len > sizeof(buf) ? sizeof(buf) : len);
                    std::string tx[4];
                    size_t n = 0;
                    if (ctl.onFrame(frame, now, Board::utc(), Drv::rotor(), tx, 4, n)) {
                        last = ctl.lastResult();
                        logf("APRS: %s\n", last.c_str());
                        for (size_t i = 0; i < n; i++) send(tx[i]);
                    }
                }
                phy->startReceive();
            }
        }
        // Baliza de estado
        if (Cfg.beaconMin > 0 && (lastBeacon == 0 || now - lastBeacon >= (uint32_t)Cfg.beaconMin * 60000UL)) {
            lastBeacon = now ? now : 1;
            send(beaconFrame());
        }
        // Emision: deja 1,5 s tras una recepcion o emision (el otro extremo cambia a recepcion)
        if (!transmitting && !txq.empty() && now - rxAt > 1500 && now - txDoneAt > 1500) {
            static std::string cur;
            cur = std::string("<\xff\x01", 3) + txq.front();
            txq.pop_front();
            irq = false;
            transmitting = true;
            if (phy->startTransmit((const uint8_t*)cur.data(), cur.size()) != RADIOLIB_ERR_NONE) startRx();
        }
    }
}
