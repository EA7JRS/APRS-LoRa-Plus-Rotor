/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "config.h"
#include <LittleFS.h>

RotorConfig Cfg;

static const char* CONF_FILE = "/rotor_conf.json";
static const char* MASK = "********";

namespace Config {

    void defaults(RotorConfig& c) {
        c = RotorConfig();
        // Yaesu G-5500 / G-5500DC: el mando da 2-4,5 V por el DIN (acimut 0-450, elevacion 0-180). Con el divisor de
        // docs/HARDWARE.md (10 k / 18 k, factor 0,643) llegan 1,286-2,893 V a la placa. Se afina luego con "Punto A / B".
        c.az.minDeg = 0;  c.az.maxDeg = 450;
        c.el.minDeg = 0;  c.el.maxDeg = 180;
    #if defined(DEF_RAW_A) && defined(DEF_RAW_B)
        c.az.rawA = DEF_RAW_A; c.az.rawB = DEF_RAW_B;   // la placa da cuentas (p. ej. EXIO_ADC del expansor)
    #elif ROTOR_ESP32
        c.az.rawA = 1286; c.az.rawB = 2893;        // milivoltios
    #else
        c.az.rawA = 1596; c.az.rawB = 3590;        // cuentas de 12 bits con referencia de 3,3 V
    #endif
        c.el.rawA = c.az.rawA; c.el.rawB = c.az.rawB;
        c.az.degA = 0; c.az.degB = 450;
        c.el.degA = 0; c.el.degB = 180;
        c.azPins.pinA = DEF_AZ_CW;   c.azPins.pinB = DEF_AZ_CCW;  c.azPins.adcPin = DEF_AZ_ADC; c.azPins.adsChannel = 0;
        c.elPins.pinA = DEF_EL_UP;   c.elPins.pinB = DEF_EL_DOWN; c.elPins.adcPin = DEF_EL_ADC; c.elPins.adsChannel = 1;
        c.aprs.callsign = "N0CALL-12";
    #ifdef DEF_EL_ENABLED
        c.elEnabled = DEF_EL_ENABLED;              // placas sin segundo ADC: la elevacion necesita un ADS1115
    #endif
    #ifdef DEF_ACTIVE_HIGH
        c.activeHigh = DEF_ACTIVE_HIGH;            // placas de relés por PCF8574: activan con 0
    #endif
    }

    // ------------------------------------------------------------------ JSON ------------------------------------------------------------
    static void putAxis(JsonObject o, const RC::AxisCfg& a, const AxisPins& p) {
        o["min"] = a.minDeg;         o["max"] = a.maxDeg;
        o["rawA"] = a.rawA;          o["degA"] = a.degA;
        o["rawB"] = a.rawB;          o["degB"] = a.degB;
        o["deadband"] = a.deadband;  o["restart"] = a.restart;
        o["slowZone"] = a.slowZone;  o["minSpeed"] = a.minSpeed;  o["maxSpeed"] = a.maxSpeed;
        o["rampMs"] = a.rampMs;      o["reverseMs"] = a.reverseDelayMs;
        o["stallMs"] = a.stallMs;    o["stallDeg"] = a.stallDeg;
        o["brakeMs"] = a.brakeMs;    o["filter"] = a.filter;      o["limitMargin"] = a.limitMargin;
        o["pinA"] = p.pinA;          o["pinB"] = p.pinB;          o["pinPwm"] = p.pinPwm;
        o["sensor"] = p.sensor;      o["adcPin"] = p.adcPin;      o["adsCh"] = p.adsChannel;
    }

    template <typename T> static void get(JsonVariantConst o, const char* k, T& v) { if (o[k].is<T>()) v = o[k].as<T>(); }
    static void getD(JsonVariantConst o, const char* k, double& v) { if (o[k].is<double>()) v = o[k].as<double>(); }
    static void getU(JsonVariantConst o, const char* k, uint32_t& v) { if (o[k].is<unsigned long>()) v = o[k].as<unsigned long>(); }
    static void getS(JsonVariantConst o, const char* k, std::string& v) { if (o[k].is<const char*>()) v = o[k].as<const char*>(); }
    static void getSecret(JsonVariantConst o, const char* k, std::string& v) {   // "********" = no cambiar
        if (o[k].is<const char*>() && strcmp(o[k].as<const char*>(), MASK) != 0) v = o[k].as<const char*>();
    }

    static void getAxis(JsonVariantConst o, RC::AxisCfg& a, AxisPins& p) {
        if (!o.is<JsonObjectConst>()) return;
        getD(o, "min", a.minDeg);        getD(o, "max", a.maxDeg);
        getD(o, "rawA", a.rawA);         getD(o, "degA", a.degA);
        getD(o, "rawB", a.rawB);         getD(o, "degB", a.degB);
        getD(o, "deadband", a.deadband); getD(o, "restart", a.restart);
        getD(o, "slowZone", a.slowZone); getD(o, "minSpeed", a.minSpeed); getD(o, "maxSpeed", a.maxSpeed);
        getU(o, "rampMs", a.rampMs);     getU(o, "reverseMs", a.reverseDelayMs);
        getU(o, "stallMs", a.stallMs);   getD(o, "stallDeg", a.stallDeg);
        getU(o, "brakeMs", a.brakeMs);   getD(o, "filter", a.filter);  getD(o, "limitMargin", a.limitMargin);
        get(o, "pinA", p.pinA);          get(o, "pinB", p.pinB);       get(o, "pinPwm", p.pinPwm);
        get(o, "sensor", p.sensor);      get(o, "adcPin", p.adcPin);   get(o, "adsCh", p.adsChannel);
    }

    std::string toJson(const RotorConfig& c, bool secrets) {
        JsonDocument d;
        d["ver"] = ROTOR_VERSION;
        d["name"] = c.name;
        d["pin"] = secrets ? c.pin : std::string(MASK);
        JsonObject w = d["wifi"].to<JsonObject>();
        w["mode"] = c.wifiMode; w["ssid"] = c.ssid; w["pass"] = secrets ? c.pass : std::string(MASK);
        w["apPass"] = secrets ? c.apPass : std::string(MASK); w["ntp"] = c.ntp;
        JsonObject n = d["net"].to<JsonObject>();
        n["rotctld"] = c.rotctld; n["rotctldPort"] = c.rotctldPort; n["apiPort"] = c.apiPort;
        n["webUser"] = c.webUser; n["webPass"] = secrets ? c.webPass : std::string(MASK);
        d["usb"]["proto"] = c.usbProto;
        d["bt"]["mode"] = c.btMode;
        d["bt"]["sppProto"] = c.sppProto;
        d["driver"] = c.driver;
        JsonObject dr = d["direct"].to<JsonObject>();
        dr["outMode"] = c.outMode; dr["activeHigh"] = c.activeHigh; dr["brakePin"] = c.brakePin; dr["pwmFreq"] = c.pwmFreq;
        dr["i2cSda"] = c.i2cSda; dr["i2cScl"] = c.i2cScl; dr["adsAddr"] = c.adsAddr; dr["adsGain"] = c.adsGain; dr["pcfAddr"] = c.pcfAddr;
        dr["elEnabled"] = c.elEnabled; dr["parkAz"] = c.parkAz; dr["parkEl"] = c.parkEl; dr["reportMech"] = c.reportMech;
        putAxis(dr["az"].to<JsonObject>(), c.az, c.azPins);
        putAxis(dr["el"].to<JsonObject>(), c.el, c.elPins);
        JsonObject s = d["serial"].to<JsonObject>();
        s["proto"] = c.serProto; s["baud"] = c.serBaud; s["rx"] = c.serRx; s["tx"] = c.serTx; s["hasEl"] = c.serHasEl;
        s["busId"] = c.serBusId; s["azId"] = c.serAzId; s["elId"] = c.serElId;
        s["pollMs"] = c.serPollMs; s["minAz"] = c.serMinAz; s["maxAz"] = c.serMaxAz; s["maxEl"] = c.serMaxEl;
        JsonObject l = d["lora"].to<JsonObject>();
        l["chip"] = c.loraChip; l["freq"] = c.loraFreq; l["bw"] = c.loraBw; l["sf"] = c.loraSf; l["cr"] = c.loraCr;
        l["power"] = c.loraPower; l["tcxo"] = c.loraTcxo;
        l["sck"] = c.loraSck; l["miso"] = c.loraMiso; l["mosi"] = c.loraMosi; l["cs"] = c.loraCs;
        l["rst"] = c.loraRst; l["irq"] = c.loraIrq; l["busy"] = c.loraBusy;
        JsonObject a = d["aprs"].to<JsonObject>();
        a["call"] = c.aprs.callsign; a["tocall"] = c.aprs.tocall; a["path"] = c.aprs.path; a["allow"] = c.aprs.allow;
        a["totp"] = secrets ? c.aprs.totpSecret : (c.aprs.totpSecret.empty() ? std::string() : std::string(MASK));
        a["reply"] = c.aprs.reply; a["beaconMin"] = c.beaconMin; a["lat"] = c.lat; a["lon"] = c.lon; a["comment"] = c.comment;
        d["led"] = c.led;
        JsonObject ct = d["cat"].to<JsonObject>();
        ct["proto"] = c.catProto; ct["baud"] = c.catBaud; ct["rx"] = c.catRx; ct["tx"] = c.catTx; ct["civ"] = c.catCiv;
        ct["model"] = c.catModel; ct["pollMs"] = c.catPollMs; ct["ptt"] = c.catPtt; ct["pttMaxS"] = c.catPttMaxS; ct["trackStep"] = c.catTrackStep;
        ct["rigctld"] = c.rigctld; ct["rigctldPort"] = c.rigctldPort; ct["host"] = c.catHost; ct["port"] = c.catPort;
        JsonObject wd = d["wind"].to<JsonObject>();
        wd["on"] = c.windOn; wd["park"] = c.windParkMs; wd["resume"] = c.windResumeMs; wd["hold"] = c.windHoldMin;
        wd["anemoPin"] = c.anemoPin; wd["kmhPerHz"] = c.anemoKmhHz; wd["pullup"] = c.anemoPullup;
        JsonObject ln = d["lan"].to<JsonObject>();
        ln["kind"] = c.lanKind; ln["host"] = c.lanHost; ln["port"] = c.lanPort; ln["udp"] = c.lanUdp;
        ln["owmKey"] = secrets ? c.owmKey : (c.owmKey.empty() ? std::string() : std::string(MASK));
        ln["wind"] = c.lanWind; ln["pos"] = c.lanPos; ln["heading"] = c.headComp; ln["headStep"] = c.headStep; ln["magVar"] = c.headVar;
        ln["magAuto"] = c.headVarAuto;
        JsonObject sk = d["sky"].to<JsonObject>();
        sk["step"] = c.skyStep; sk["minEl"] = c.skyMinEl; sk["refraction"] = c.skyRefr;
    #if ROTOR_HAS_LCD
        JsonObject lc = d["lcd"].to<JsonObject>();
        lc["bright"] = c.lcdBright; lc["dimS"] = c.lcdDimS; lc["theme"] = c.lcdTheme;
    #endif
        std::string out;
        serializeJson(d, out);
        return out;
    }

    static bool validate(const RotorConfig& c, std::string& err) {
        auto axisOk = [&](const RC::AxisCfg& a, const char* n) {
            if (!(a.maxDeg > a.minDeg)) { err = std::string(n) + ": max <= min"; return false; }
            if (fabs(a.rawB - a.rawA) < 1e-6 || fabs(a.degB - a.degA) < 1e-6) { err = std::string(n) + ": calibration"; return false; }
            if (a.deadband <= 0 || a.restart < a.deadband) { err = std::string(n) + ": deadband / restart"; return false; }
            if (a.minSpeed < 0 || a.maxSpeed > 1 || a.minSpeed > a.maxSpeed) { err = std::string(n) + ": speed 0-1"; return false; }
            if (a.filter <= 0 || a.filter > 1) { err = std::string(n) + ": filter 0-1"; return false; }
            return true;
        };
        if (c.name.empty() || c.name.size() > 24) { err = "name 1-24"; return false; }
        if (!c.pin.empty() && (c.pin.size() < 4 || c.pin.size() > 16)) { err = "pin 4-16"; return false; }
        if (c.wifiMode < 0 || c.wifiMode > 2) { err = "wifi.mode"; return false; }
        if (c.apPass.size() < 8) { err = "wifi.apPass >= 8"; return false; }
        if (c.usbProto < 0 || c.usbProto > 5 || c.sppProto < 0 || c.sppProto > 4 || c.btMode < 0 || c.btMode > 2) { err = "usb / bt"; return false; }
        if (c.driver < 0 || c.driver > 1 || c.serProto < 0 || c.serProto > 4) { err = "driver"; return false; }
        if (c.serProto == 4) {
            for (int id : {c.serBusId, c.serAzId}) if (id < 1 || id > 254) { err = "serial.busId / azId 1-254"; return false; }
            if (c.serHasEl && (c.serElId < 1 || c.serElId > 254 || c.serElId == c.serAzId)) { err = "serial.elId"; return false; }
            if (c.serBusId == c.serAzId || c.serBusId == c.serElId) { err = "serial.busId"; return false; }
        }
        if (!axisOk(c.az, "az")) return false;
        if (c.elEnabled && !axisOk(c.el, "el")) return false;
        if (c.loraChip < 0 || c.loraChip > 4) { err = "lora.chip"; return false; }
        if (c.loraSf < 6 || c.loraSf > 12 || c.loraCr < 5 || c.loraCr > 8) { err = "lora sf / cr"; return false; }
        if (c.aprs.callsign.empty() || c.aprs.callsign.size() > 9) { err = "aprs.call"; return false; }
        if (!(c.windParkMs >= 3 && c.windParkMs <= 60) || !(c.windResumeMs > 0 && c.windResumeMs <= c.windParkMs) || c.windHoldMin < 0 || c.windHoldMin > 720) {
            err = "wind: park 3-60 m/s, 0 < resume <= park, hold 0-720 min"; return false;
        }
        if (c.anemoPin >= 100 || c.anemoPin < -1) { err = "wind.anemoPin (GPIO de la placa, no del expansor)"; return false; }
        if (!(c.anemoKmhHz > 0.1 && c.anemoKmhHz < 50)) { err = "wind.kmhPerHz 0.1-50"; return false; }
        if (c.lanKind < 0 || c.lanKind > 3 || c.lanPort <= 0 || c.lanPort > 65535 || c.lanUdp < 0 || c.lanUdp > 65535) { err = "lan.kind 0-3 / port / udp"; return false; }
        if (c.lanHost.size() > 39 || c.owmKey.size() > 39) { err = "lan.host / lan.owmKey (39)"; return false; }
        if (c.lanUdp && (c.lanUdp == c.rotctldPort || c.lanUdp == c.apiPort)) { err = "lan.udp"; return false; }
        if (!(c.headStep >= 0.5 && c.headStep <= 45)) { err = "lan.headStep 0.5-45"; return false; }
        if (!(c.headVar >= -90 && c.headVar <= 90)) { err = "lan.magVar -90..90"; return false; }
        if (!(c.skyStep >= 0.1 && c.skyStep <= 20)) { err = "sky.step 0.1-20"; return false; }
        if (!(c.skyMinEl >= -5 && c.skyMinEl <= 60)) { err = "sky.minEl -5..60"; return false; }
        if (c.lcdBright < 10 || c.lcdBright > 100 || c.lcdDimS < 0) { err = "lcd.bright 10-100 / lcd.dimS >= 0"; return false; }
        if (c.lcdTheme < 0 || c.lcdTheme > 1) { err = "lcd.theme 0 (app) / 1 (rotor)"; return false; }
        if (c.catProto < 0 || c.catProto > 6) { err = "cat.proto 0-6"; return false; }
        const bool catNet = RC::catIsNetwork(c.catProto);
        if (c.catProto && !catNet && (c.catRx < 0 || c.catTx < 0)) { err = "cat.rx / cat.tx"; return false; }
        if (c.catProto && !catNet && c.driver == 1 && (c.catRx == c.serRx || c.catTx == c.serTx)) { err = "cat: same pins as serial gateway"; return false; }
        if (catNet && (c.catHost.empty() || c.catPort < 0 || c.catPort > 65535)) { err = "cat.host / cat.port"; return false; }
        if (catNet && !ROTOR_HAS_WIFI) { err = "cat: network needs WiFi"; return false; }
        if (c.catBaud < 300 || c.catBaud > 921600 || c.catCiv < 0 || c.catCiv > 0xFF || c.catPollMs < 20 || c.catTrackStep < 1 || c.catPttMaxS < 5 || c.catPttMaxS > 3600) { err = "cat baud / civ / pollMs"; return false; }
        if (c.rigctldPort <= 0 || c.rigctldPort > 65535 || c.rigctldPort == c.rotctldPort || c.rigctldPort == c.apiPort) { err = "cat.rigctldPort"; return false; }
        if (!c.aprs.totpSecret.empty()) {
            uint8_t k[64];
            if (RC::Crypto::base32(c.aprs.totpSecret.c_str(), k, sizeof(k)) < 10) { err = "aprs.totp (base32, >= 16 chars)"; return false; }
        }
        return true;
    }

    bool fromJson(const std::string& json, RotorConfig& c, std::string& err) {
        JsonDocument d;
        if (deserializeJson(d, json) != DeserializationError::Ok || !d.is<JsonObject>()) { err = "bad json"; return false; }
        RotorConfig n = c;      // se parte de la actual: valen configuraciones parciales
        getS(d, "name", n.name);
        getSecret(d, "pin", n.pin);
        JsonVariantConst w = d["wifi"];
        get(w, "mode", n.wifiMode); getS(w, "ssid", n.ssid); getSecret(w, "pass", n.pass); getSecret(w, "apPass", n.apPass); getS(w, "ntp", n.ntp);
        JsonVariantConst nt = d["net"];
        get(nt, "rotctld", n.rotctld); get(nt, "rotctldPort", n.rotctldPort); get(nt, "apiPort", n.apiPort);
        getS(nt, "webUser", n.webUser); getSecret(nt, "webPass", n.webPass);
        get(d["usb"], "proto", n.usbProto);
        get(d["bt"], "mode", n.btMode);
        get(d["bt"], "sppProto", n.sppProto);
        get(d, "driver", n.driver);
        JsonVariantConst dr = d["direct"];
        get(dr, "outMode", n.outMode); get(dr, "activeHigh", n.activeHigh); get(dr, "brakePin", n.brakePin); get(dr, "pwmFreq", n.pwmFreq);
        get(dr, "i2cSda", n.i2cSda); get(dr, "i2cScl", n.i2cScl); get(dr, "adsAddr", n.adsAddr); get(dr, "adsGain", n.adsGain); get(dr, "pcfAddr", n.pcfAddr);
        get(dr, "elEnabled", n.elEnabled); getD(dr, "parkAz", n.parkAz); getD(dr, "parkEl", n.parkEl); get(dr, "reportMech", n.reportMech);
        getAxis(dr["az"], n.az, n.azPins);
        getAxis(dr["el"], n.el, n.elPins);
        JsonVariantConst s = d["serial"];
        get(s, "proto", n.serProto); get(s, "baud", n.serBaud); get(s, "rx", n.serRx); get(s, "tx", n.serTx); get(s, "hasEl", n.serHasEl);
        get(s, "busId", n.serBusId); get(s, "azId", n.serAzId); get(s, "elId", n.serElId);
        get(s, "pollMs", n.serPollMs); getD(s, "minAz", n.serMinAz); getD(s, "maxAz", n.serMaxAz); getD(s, "maxEl", n.serMaxEl);
        JsonVariantConst l = d["lora"];
        get(l, "chip", n.loraChip); getD(l, "freq", n.loraFreq); getD(l, "bw", n.loraBw); get(l, "sf", n.loraSf); get(l, "cr", n.loraCr);
        get(l, "power", n.loraPower); getD(l, "tcxo", n.loraTcxo);
        get(l, "sck", n.loraSck); get(l, "miso", n.loraMiso); get(l, "mosi", n.loraMosi); get(l, "cs", n.loraCs);
        get(l, "rst", n.loraRst); get(l, "irq", n.loraIrq); get(l, "busy", n.loraBusy);
        JsonVariantConst a = d["aprs"];
        getS(a, "call", n.aprs.callsign); getS(a, "tocall", n.aprs.tocall); getS(a, "path", n.aprs.path); getS(a, "allow", n.aprs.allow);
        getSecret(a, "totp", n.aprs.totpSecret); get(a, "reply", n.aprs.reply); get(a, "beaconMin", n.beaconMin);
        getD(a, "lat", n.lat); getD(a, "lon", n.lon); getS(a, "comment", n.comment);
        get(d, "led", n.led);
        JsonVariantConst wd = d["wind"];
        get(wd, "on", n.windOn); getD(wd, "park", n.windParkMs); getD(wd, "resume", n.windResumeMs); get(wd, "hold", n.windHoldMin);
        get(wd, "anemoPin", n.anemoPin); getD(wd, "kmhPerHz", n.anemoKmhHz); get(wd, "pullup", n.anemoPullup);
        JsonVariantConst ln = d["lan"];
        get(ln, "kind", n.lanKind); getS(ln, "host", n.lanHost); get(ln, "port", n.lanPort); get(ln, "udp", n.lanUdp);
        getSecret(ln, "owmKey", n.owmKey); get(ln, "wind", n.lanWind); get(ln, "pos", n.lanPos);
        get(ln, "heading", n.headComp); getD(ln, "headStep", n.headStep); getD(ln, "magVar", n.headVar); get(ln, "magAuto", n.headVarAuto);
        getD(d["sky"], "step", n.skyStep); getD(d["sky"], "minEl", n.skyMinEl); get(d["sky"], "refraction", n.skyRefr);
        get(d["lcd"], "bright", n.lcdBright); get(d["lcd"], "dimS", n.lcdDimS); get(d["lcd"], "theme", n.lcdTheme);
        JsonVariantConst ct = d["cat"];
        get(ct, "proto", n.catProto); get(ct, "baud", n.catBaud); get(ct, "rx", n.catRx); get(ct, "tx", n.catTx); get(ct, "civ", n.catCiv);
        getS(ct, "model", n.catModel); get(ct, "pollMs", n.catPollMs); get(ct, "ptt", n.catPtt); get(ct, "pttMaxS", n.catPttMaxS); get(ct, "trackStep", n.catTrackStep);
        get(ct, "rigctld", n.rigctld); get(ct, "rigctldPort", n.rigctldPort); getS(ct, "host", n.catHost); get(ct, "port", n.catPort);
        n.aprs.callsign = RC::Aprs::upper(n.aprs.callsign);
        if (!validate(n, err)) return false;
        c = n;
        return true;
    }

    bool load() {
        defaults(Cfg);
        File f = LittleFS.open(CONF_FILE, "r");
        if (!f) return false;
        std::string s;
        s.reserve(f.size());
        while (f.available()) s += (char)f.read();
        f.close();
        std::string err;
        if (!fromJson(s, Cfg, err)) { logf("config: %s (se usan los valores por defecto)\n", err.c_str()); defaults(Cfg); return false; }
        return true;
    }

    bool save() {
        const std::string s = toJson(Cfg, true);
        File f = LittleFS.open("/rotor_conf.tmp", "w");
        if (!f) return false;
        const size_t w = f.write((const uint8_t*)s.data(), s.size());
        f.close();
        if (w != s.size()) return false;
        LittleFS.remove(CONF_FILE);
        return LittleFS.rename("/rotor_conf.tmp", CONF_FILE);
    }
}
