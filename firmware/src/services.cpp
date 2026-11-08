/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "services.h"
#include <stdarg.h>
#include "config.h"
#include "drivers.h"
#include "board.h"
#include "lora_aprs.h"
#include "net.h"
#include "bt.h"
#if ROTOR_ESP32
    #include <esp_ota_ops.h>
#endif

RotorApiHost ApiHost;

void logf(const char* fmt, ...) {
    if (Cfg.usbProto != 5) return;
    char b[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    Serial.print(b);
}

// ---------------------------------------------------------------------------------------------------------------------------
void ProtoPort::poll(Stream& s, RC::Rotor& rot) {
    uint8_t buf[64];
    int n = 0;
    while (s.available() && n < (int)sizeof(buf)) {
        const int c = s.read();
        if (c < 0) break;
        buf[n++] = (uint8_t)c;
    }
    if (n == 0) return;
    probe(s, buf, n);
    std::string out;
    switch (proto_) {
        case 0: case 1: gs_.feed((const char*)buf, n, rot, out); break;
        case 2: ec_.feed((const char*)buf, n, rot, out); break;
        case 3: r2p_.feed(buf, n, rot, out); break;
        case 4: rc_.feed((const char*)buf, n, rot, out); break;
        default: return;
    }
    if (!out.empty()) s.write((const uint8_t*)out.data(), out.size());
}

void ProtoPort::probe(Stream& s, const uint8_t* buf, int n) {
    for (int i = 0; i < n; i++) {
        const char c = (char)buf[i];
        if (c == '\n' || c == '\r') {
            if (probe_ == "APRS-INFO?") {
                bool ota = false;
            #if ROTOR_ESP32
                ota = esp_ota_get_next_update_partition(nullptr) != nullptr;
            #else
                ota = ROTOR_HAS_WIFI != 0;
            #endif
            #if ROTOR_ESP32
                const char* chip = CONFIG_IDF_TARGET;
            #else
                const char* chip = "rp2350";
            #endif
                char line[160];
                snprintf(line, sizeof(line), "\r\nAPRS-LoRa-Plus: device=rotor board=%s version=%s chip=%s ota=%d\r\n", ROTOR_BOARD, ROTOR_VERSION, chip, ota ? 1 : 0);
                s.print(line);
            }
            probe_.clear();
        } else if (probe_.size() < 16 && c >= ' ' && c < 127) {
            probe_ += c;
        } else {
            probe_.clear();
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------
RC::Rotor& RotorApiHost::rotor() { return Drv::rotor(); }

void RotorApiHost::info(JsonObject o) {
    o["dev"] = "APRS LoRa+ Rotor";
    o["ver"] = ROTOR_VERSION;
    o["board"] = ROTOR_BOARD;
    o["chip"] = Board::chip();
    o["name"] = Cfg.name;
    o["drv"] = Drv::name();
    o["call"] = Cfg.aprs.callsign;
    JsonObject caps = o["caps"].to<JsonObject>();
    caps["wifi"] = ROTOR_HAS_WIFI != 0;
    caps["ble"] = ROTOR_HAS_BLE != 0;
    caps["spp"] = ROTOR_HAS_SPP != 0;
    caps["lora"] = Cfg.loraChip != 0;
    caps["cal"] = Cfg.driver == 0;
    caps["move"] = !(Cfg.driver == 1 && Cfg.serProto == RC::SP_ROT2PROG);
    caps["lcd"] = ROTOR_HAS_LCD != 0;
    o["ip"] = Net::ip();
    o["rotctld"] = Cfg.rotctld ? Cfg.rotctldPort : 0;
    o["heap"] = Board::freeHeap();
    o["up"] = millis() / 1000;
    o["utc"] = (unsigned long long)Board::utc();
}

void RotorApiHost::posExtra(JsonObject o) {
    Drv::posExtra(o);
    const std::string a = LoraAprs::lastResult();
    if (!a.empty()) o["aprs"] = a;
}

std::string RotorApiHost::configJson() { return Config::toJson(Cfg, true); }

bool RotorApiHost::applyConfig(const std::string& json, std::string& err) {
    RotorConfig n = Cfg;
    if (!Config::fromJson(json, n, err)) return false;
    Drv::allOff();
    Cfg = n;
    if (!Config::save()) { err = "save failed"; return false; }
    requestReboot(1500);        // pines, red y radio se aplican al arrancar
    return true;
}

bool RotorApiHost::calibrate(const char* axis, const char* pt, double deg, std::string& err) { return Drv::calibrate(axis, pt, deg, err); }
bool RotorApiHost::setHere(const char* axis, double deg, std::string& err) { return Drv::setHere(axis, deg, err); }
void RotorApiHost::raw(JsonObject o) { Drv::raw(o); }
void RotorApiHost::clearFault() { Drv::clearFault(); }
void RotorApiHost::reboot() { requestReboot(); }
const char* RotorApiHost::pin() { return Cfg.pin.c_str(); }
std::string RotorApiHost::lastAprs() { return LoraAprs::lastResult(); }

// ---------------------------------------------------------------------------------------------------------------------------
static uint32_t rebootAt = 0;
void requestReboot(uint32_t afterMs) { rebootAt = millis() + afterMs; if (!rebootAt) rebootAt = 1; }
void serviceReboot() {
    if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
        Drv::allOff();
        Board::reboot();
    }
}

// ---------------------------------------------------------------------------------------------------------------------------
void ApiClient::feed(const char* data, size_t n, std::vector<std::string>& out) {
    for (size_t i = 0; i < n; i++) {
        const char c = data[i];
        if (c == '\n') {
            if (!in.empty()) sess.handle(in, ApiHost, out);
            in.clear();
        } else if (c != '\r') {
            if (in.size() < 1024) in += c;
            else in.clear();
        }
    }
}

void ApiClient::tick(std::vector<std::string>& out) {
    if (!sess.subMs) return;
    const uint32_t now = millis();
    const bool moving = (Drv::rotor().status() & (RC::ST_MOVING | RC::ST_BUSY)) != 0;
    const uint32_t every = moving ? sess.subMs : (sess.subMs > 5000 ? sess.subMs : 5000);
    if (moving != wasMoving || now - lastEvt >= every) {
        wasMoving = moving;
        lastEvt = now;
        out.push_back(RC::ApiSession::posEvent(ApiHost));
    }
}
