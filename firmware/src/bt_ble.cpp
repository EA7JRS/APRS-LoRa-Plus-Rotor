/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 *
 * BLE (NimBLE): servicio de control del rotor con el protocolo JSON por lineas (docs/CONTROL_PROTOCOL.md).
 *   Servicio  4c6f5261-2b00-4ea7-b000-000000000001
 *   Ordenes   ...0002  (el movil escribe lineas JSON; con o sin respuesta)
 *   Eventos   ...0003  (el rotor notifica lineas JSON terminadas en '\n')
 * Enlace cifrado (emparejamiento "Just Works" con vinculacion) y PIN de control en la orden "auth".
 */

#include "board.h"
#if ROTOR_HAS_BLE

#include <NimBLEDevice.h>
#include <mutex>
#include <deque>
#include "config.h"
#include "services.h"

#define ROT_SVC_UUID "4c6f5261-2b00-4ea7-b000-000000000001"
#define ROT_CMD_UUID "4c6f5261-2b00-4ea7-b000-000000000002"
#define ROT_EVT_UUID "4c6f5261-2b00-4ea7-b000-000000000003"

namespace BtBle {

    static NimBLECharacteristic* evt = nullptr;
    static std::mutex            mtx;
    static std::deque<std::string> inq;
    static volatile bool         conn = false, resetSess = false;
    static volatile uint16_t     mtu = 23;
    static ApiClient             client;

    class ServerCb : public NimBLEServerCallbacks {
        void onConnect(NimBLEServer*, NimBLEConnInfo& info) override { conn = true; resetSess = true; mtu = 23; }
        void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { conn = false; resetSess = true; }
        void onMTUChange(uint16_t m, NimBLEConnInfo&) override { mtu = m; }
    };

    class CmdCb : public NimBLECharacteristicCallbacks {
        void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
            const NimBLEAttValue v = c->getValue();
            std::lock_guard<std::mutex> lk(mtx);
            if (inq.size() < 32) inq.emplace_back((const char*)v.data(), v.length());
        }
    };

    void begin() {
        NimBLEDevice::init(Cfg.name);
        NimBLEDevice::setSecurityAuth(true, false, true);              // vinculacion, sin MITM, LE Secure Connections
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
        NimBLEDevice::setMTU(247);
        NimBLEServer* srv = NimBLEDevice::createServer();
        srv->setCallbacks(new ServerCb());
        NimBLEService* svc = srv->createService(ROT_SVC_UUID);
        NimBLECharacteristic* cmd = svc->createCharacteristic(ROT_CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);
        cmd->setCallbacks(new CmdCb());
        evt = svc->createCharacteristic(ROT_EVT_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC);
        evt->setValue("{\"e\":\"hello\",\"dev\":\"APRS LoRa+ Rotor\"}\n");
        svc->start();
        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        adv->addServiceUUID(ROT_SVC_UUID);
        adv->setName(Cfg.name);
        adv->enableScanResponse(true);
        adv->start();
        logf("BLE listo: %s\n", Cfg.name.c_str());
    }

    static void sendLine(const std::string& l) {
        if (!evt || !conn) return;
        std::string s = l + "\n";
        const size_t chunk = mtu > 23 ? mtu - 3 : 20;
        for (size_t i = 0; i < s.size(); i += chunk) {
            const size_t n = std::min(chunk, s.size() - i);
            evt->notify((const uint8_t*)s.data() + i, n);
        }
    }

    void loop() {
        if (resetSess) { resetSess = false; client.reset(); }
        std::vector<std::string> out;
        for (;;) {
            std::string pkt;
            {
                std::lock_guard<std::mutex> lk(mtx);
                if (inq.empty()) break;
                pkt = std::move(inq.front());
                inq.pop_front();
            }
            client.feed(pkt.data(), pkt.size(), out);
        }
        if (conn) client.tick(out);
        for (auto& l : out) sendLine(l);
    }

    bool connected() { return conn; }
}

#endif
