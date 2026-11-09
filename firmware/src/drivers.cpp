/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "drivers.h"
#include <Wire.h>
#include <vector>
#include "config.h"
#include "board.h"
#include "rc_control.h"
#include "rc_serialproto.h"
#include "exio.h"

namespace Drv {

    // =========================================================================================================================
    // Sensores: ADC de la placa o ADS1115 (16 bits, I2C). El ADS1115 se lee sin bloquear: un canal por vuelta
    // =========================================================================================================================
    namespace Ads {
        static bool     ok = false;
        static int      cur = -1;          // canal en conversion
        static uint32_t startedAt = 0;
        static double   value[4] = { NAN, NAN, NAN, NAN };

        static bool writeReg(uint8_t reg, uint16_t v) {
            Wire.beginTransmission((uint8_t)Cfg.adsAddr);
            Wire.write(reg); Wire.write((uint8_t)(v >> 8)); Wire.write((uint8_t)v);
            return Wire.endTransmission() == 0;
        }
        static bool readConv(int16_t& v) {
            Wire.beginTransmission((uint8_t)Cfg.adsAddr);
            Wire.write((uint8_t)0x00);
            if (Wire.endTransmission() != 0) return false;
            if (Wire.requestFrom((uint8_t)Cfg.adsAddr, (uint8_t)2) != 2) return false;
            v = (int16_t)((Wire.read() << 8) | Wire.read());
            return true;
        }
        static void start(int ch) {
            const uint16_t pga = (uint16_t)(Cfg.adsGain & 7) << 9;
            const uint16_t cfg = 0x8000 | (uint16_t)((4 + (ch & 3)) << 12) | pga | 0x0100 | 0x00E0 | 0x0003;   // un disparo, 860 SPS
            ok = writeReg(0x01, cfg);
            cur = ch;
            startedAt = millis();
        }
        static void begin(bool needed) {
            if (!needed || !Board::i2cBegin()) return;
            start(Cfg.azPins.adsChannel);
        }
        // Llamar a menudo: alterna los canales del acimut y de la elevacion
        static void poll(int chA, int chB) {
            if (cur < 0) return;
            if (millis() - startedAt < 3) return;
            int16_t v;
            if (readConv(v)) value[cur & 3] = v; else { value[cur & 3] = NAN; ok = false; }
            const int next = (cur == chA && chB >= 0) ? chB : chA;
            start(next);
        }
    }

    // =========================================================================================================================
    // Control directo
    // =========================================================================================================================
    static RC::DirectRotor direct;
    static bool            usingAds = false;
    static double          rawAz = NAN, rawEl = NAN;

    static double readSensor(const AxisPins& p, bool ads) {
        if (p.sensor == 1) return ads ? Ads::value[p.adsChannel & 3] : NAN;
        if (p.sensor == 0) return Board::readAdc(p.adcPin);
        return NAN;
    }

    static void outPin(int pin, bool on) {
        if (pin < 0) return;
        Board::digitalOut(pin, on ? Cfg.activeHigh : !Cfg.activeHigh);
    }

    static void applyAxis(const AxisPins& p, const RC::AxisOut& o) {
        if (Cfg.outMode == 1) {                         // puente H: PWM en IN1 o IN2
            Board::pwmWrite(p.pinA, o.dir > 0 ? o.speed : 0);
            Board::pwmWrite(p.pinB, o.dir < 0 ? o.speed : 0);
        } else {                                        // reles: CW / CCW (UP / DOWN), velocidad aparte si hay pin
            outPin(p.pinA, o.dir > 0);
            outPin(p.pinB, o.dir < 0);
            if (p.pinPwm >= 0) Board::pwmWrite(p.pinPwm, o.dir ? o.speed : 0);
        }
    }

    static void setupAxisPins(const AxisPins& p) {
        if (Cfg.outMode == 1) {
            Board::pwmAttach(p.pinA, Cfg.pwmFreq);
            Board::pwmAttach(p.pinB, Cfg.pwmFreq);
        } else {
            for (int pin : { p.pinA, p.pinB }) if (pin >= 0) { outPin(pin, false); Board::pinOutput(pin); outPin(pin, false); }
            if (p.pinPwm >= 0) Board::pwmAttach(p.pinPwm, Cfg.pwmFreq);
        }
        if (p.sensor == 0 && p.adcPin >= 0 && p.adcPin < PIN_EXIO_BASE) pinMode(p.adcPin, INPUT);
    }

    // =========================================================================================================================
    // Pasarela serie
    // =========================================================================================================================
    class SerialRotor : public RC::Rotor {
    public:
        Stream*  port = nullptr;
        double   az = 0, el = 0, taz = NAN, tel = NAN;
        bool     known = false;
        uint32_t lastReply = 0, lastPoll = 0, lastMoveSeen = 0;
        int      manualDir = 0;
        char     info_[48];

        void send(const std::string& s) { if (port) port->write((const uint8_t*)s.data(), s.size()); }
        void sendBin(const uint8_t* b, size_t n) { if (port) port->write(b, n); }

        bool setPos(double a, double e) override {
            taz = a; tel = e; manualDir = 0;
            switch (Cfg.serProto) {
                case RC::SP_GS232A: case RC::SP_GS232B: send(RC::GS232::cmdSet(a, e, Cfg.serHasEl)); break;
                case RC::SP_EASYCOMM2: send(RC::EasyComm::cmdSet(a, e)); break;
                case RC::SP_ROT2PROG: { uint8_t f[13]; RC::Rot2Prog::frameSet(f, a, Cfg.serHasEl ? e : 0); sendBin(f, 13); break; }
                case RC::SP_RSBUS:
                    busPush(Cfg.serAzId, "SETPOSDG", RC::RsBus::fmtDeg(a), true);
                    if (Cfg.serHasEl) busPush(Cfg.serElId, "SETPOSDG", RC::RsBus::fmtDeg(e), true);
                    break;
            }
            return true;
        }
        bool setAz(double a) override {
            if (Cfg.serProto == RC::SP_RSBUS) { taz = a; manualDir = 0; busPush(Cfg.serAzId, "SETPOSDG", RC::RsBus::fmtDeg(a), true); return true; }
            return setPos(a, known ? el : 0);
        }
        void getPos(double& a, double& e) override { a = az; e = el; }
        bool stop() override {
            taz = tel = NAN; manualDir = 0;
            switch (Cfg.serProto) {
                case RC::SP_GS232A: case RC::SP_GS232B: send(RC::GS232::cmdStop()); break;
                case RC::SP_EASYCOMM2: send(RC::EasyComm::cmdStop()); break;
                case RC::SP_ROT2PROG: { uint8_t f[13]; RC::Rot2Prog::frameSimple(f, RC::Rot2Prog::K_STOP); sendBin(f, 13); break; }
                case RC::SP_RSBUS:
                    busQ.clear();                                   // la parada va antes que nada
                    busPush(Cfg.serAzId, "STOP", "0", true);
                    if (Cfg.serHasEl) busPush(Cfg.serElId, "STOP", "0", true);
                    break;
            }
            return true;
        }
        bool park() override { return setPos(Cfg.parkAz, Cfg.parkEl); }
        bool move(int dir, int pct) override {
            if (Cfg.serProto == RC::SP_ROT2PROG) return false;
            if (!Cfg.serHasEl && (dir == RC::MOVE_UP || dir == RC::MOVE_DOWN)) return false;
            if (Cfg.serProto == RC::SP_RSBUS) {
                // sin orden de mover: ir hacia el tope de ese lado hasta que llegue la parada
                taz = tel = NAN;
                manualDir = dir;
                if (dir == RC::MOVE_LEFT)  busPush(Cfg.serAzId, "SETPOSDG", RC::RsBus::fmtDeg(Cfg.serMinAz), true);
                if (dir == RC::MOVE_RIGHT) busPush(Cfg.serAzId, "SETPOSDG", RC::RsBus::fmtDeg(Cfg.serMaxAz), true);
                if (dir == RC::MOVE_DOWN)  busPush(Cfg.serElId, "SETPOSDG", RC::RsBus::fmtDeg(0), true);
                if (dir == RC::MOVE_UP)    busPush(Cfg.serElId, "SETPOSDG", RC::RsBus::fmtDeg(Cfg.serMaxEl), true);
                return true;
            }
            taz = tel = NAN;
            manualDir = dir;
            if (Cfg.serProto == RC::SP_EASYCOMM2) send(RC::EasyComm::cmdMove(dir));
            else { if (pct > 0) send(RC::GS232::cmdSpeed(pct)); send(RC::GS232::cmdMove(dir)); }
            return true;
        }
        uint32_t status() override {
            uint32_t s = 0;
            const bool moving = millis() - lastMoveSeen < 1500;
            if (moving || manualDir) s |= RC::ST_MOVING;
            if (!isnan(taz) && fabs(RC::angleDiff(az, taz)) > 2.0) s |= RC::ST_BUSY | RC::ST_MOVING_AZ;
            if (Cfg.serHasEl && !isnan(tel) && fabs(tel - el) > 2.0) s |= RC::ST_BUSY | RC::ST_MOVING_EL;
            return s;
        }
        const char* info() override { snprintf(info_, sizeof(info_), "APRS LoRa+ Rotor (%s)", RC::serialProtoName(Cfg.serProto)); return info_; }
        double minAz() const override { return Cfg.serMinAz; }
        double maxAz() const override { return Cfg.serMaxAz; }
        double minEl() const override { return 0; }
        double maxEl() const override { return Cfg.serHasEl ? Cfg.serMaxEl : 0; }
        bool   hasEl() const override { return Cfg.serHasEl; }

        bool linkOk() const { return lastReply && millis() - lastReply < 3000; }

        // ---- bus RS485: una trama cada vez (semidúplex); la siguiente tras la respuesta o 200 ms
        struct BusItem { std::string data; bool urgent; };
        std::vector<BusItem> busQ;
        RC::RsBus::Reader    busRd;
        bool     busWaiting = false;
        uint32_t busSentAt = 0;
        uint8_t  busErr = 0, busErrAxis = 0;               // último ERR (código del controlador) y eje (1 AZ, 2 EL)
        bool     busAskEl = false;

        void busPush(int dst, const char* cmd, const std::string& params, bool urgent = false) {
            BusItem it{RC::RsBus::frame(Cfg.serBusId, dst, cmd, params), urgent};
            if (urgent) {
                size_t i = 0;
                while (i < busQ.size() && busQ[i].urgent) i++;
                busQ.insert(busQ.begin() + i, it);
            } else if (busQ.size() < 8) {
                busQ.push_back(it);
            }
        }
        void busPump(uint32_t now) {
            if (busWaiting && now - busSentAt < 200) return;
            busWaiting = false;
            if (busQ.empty()) return;
            send(busQ.front().data);
            busQ.erase(busQ.begin());
            busWaiting = true;
            busSentAt = now;
        }
        void busGot(const RC::RsBus::Msg& m) {
            const bool isAz = m.src == Cfg.serAzId, isEl = Cfg.serHasEl && m.src == Cfg.serElId;
            if (!isAz && !isEl) return;
            if (m.dst == Cfg.serBusId) busWaiting = false;
            if (m.cmd == "ERR" && (m.dst == RC::RsBus::BROADCAST || m.dst == Cfg.serBusId)) {
                busErr = (uint8_t)RC::RsBus::num(m.params);
                busErrAxis = isAz ? 1 : 2;
                if (busErr) { taz = tel = NAN; manualDir = 0; }
                lastReply = millis();
                return;
            }
            if (m.dst != Cfg.serBusId) return;
            if (m.cmd == "ACK_GETPOSDG") {
                const double v = RC::RsBus::num(m.params);
                if (isAz) gotPos(v, el, false); else gotPos(az, v, true);
            } else {
                lastReply = millis();
            }
        }
        uint8_t busFault() const {
            if (!busErr) return RC::F_NONE;
            if (busErr == 12) return RC::F_ESTOP;
            if (busErr == 15 || busErr == 16 || busErr == 18) return busErrAxis == 2 ? RC::F_STALL_EL : RC::F_STALL_AZ;
            return RC::F_SENSOR;
        }
        void busClear() {
            if (!busErr) return;
            busPush(busErrAxis == 2 ? Cfg.serElId : Cfg.serAzId, "SETREF", "0", true);   // borra el fallo sin buscar la referencia
            busErr = 0;
        }

        void gotPos(double a, double e, bool hasE) {
            if (known && (fabs(a - az) > 0.4 || (hasE && fabs(e - el) > 0.4))) lastMoveSeen = millis();
            az = a;
            if (hasE) el = e;
            known = true;
            lastReply = millis();
        }

        // Respuestas
        std::string line;
        uint8_t     bin[12];
        size_t      binLen = 0;
        bool        askEl = false;

        void poll() {
            if (!port) return;
            while (port->available()) {
                const int c = port->read();
                if (c < 0) break;
                if (Cfg.serProto == RC::SP_RSBUS) {
                    RC::RsBus::Msg m;
                    if (busRd.push((char)c, m)) busGot(m);
                    continue;
                }
                if (Cfg.serProto == RC::SP_ROT2PROG) {
                    if (binLen == 0 && c != 0x57) continue;
                    bin[binLen++] = (uint8_t)c;
                    if (binLen == 12) { double a, e; if (RC::Rot2Prog::parseReply(bin, a, e)) gotPos(a, e, Cfg.serHasEl); binLen = 0; }
                    continue;
                }
                if (c == '\n' || c == '\r') {
                    if (!line.empty()) {
                        double a = az, e = el;
                        if (Cfg.serProto == RC::SP_EASYCOMM2) {
                            bool ga, ge;
                            if (RC::EasyComm::parseReply(line.c_str(), a, e, ga, ge)) {
                                if (ga) gotPos(a, el, false);
                                if (ge) gotPos(az, e, true);
                            }
                        } else {
                            bool he;
                            if (RC::GS232::parsePos(line.c_str(), a, e, he)) {
                                if (a >= 0) gotPos(a, e, he); else gotPos(az, e, true);
                            }
                        }
                        line.clear();
                    }
                } else if (line.size() < 64) line += (char)c;
                else line.clear();
            }
            const uint32_t now = millis();
            if (now - lastPoll >= (uint32_t)Cfg.serPollMs) {
                lastPoll = now;
                switch (Cfg.serProto) {
                    case RC::SP_GS232A: case RC::SP_GS232B: send(RC::GS232::cmdQuery(Cfg.serHasEl)); break;
                    case RC::SP_EASYCOMM2:
                        // AZ y EL alternos (Hamlib los pide por separado)
                        send(askEl && Cfg.serHasEl ? RC::EasyComm::cmdQueryEl() : RC::EasyComm::cmdQueryAz());
                        askEl = !askEl;
                        break;
                    case RC::SP_ROT2PROG: { uint8_t f[13]; RC::Rot2Prog::frameSimple(f, RC::Rot2Prog::K_STATUS); sendBin(f, 13); break; }
                    case RC::SP_RSBUS:
                        // AZ y EL alternos; las consultas mantienen vivo al controlador mientras se mueve
                        if (busQ.size() < 2) busPush(busAskEl && Cfg.serHasEl ? Cfg.serElId : Cfg.serAzId, "GETPOSDG", "0");
                        busAskEl = !busAskEl;
                        break;
                }
            }
            if (Cfg.serProto == RC::SP_RSBUS) busPump(now);
        }
    };
    static SerialRotor serial;

    static void openSerial() {
    #if ROTOR_ESP32
        #if defined(SOC_UART_HP_NUM) && SOC_UART_HP_NUM < 3
            Serial1.begin(Cfg.serBaud, SERIAL_8N1, Cfg.serRx, Cfg.serTx);    // ESP32-C5 / C6: solo dos UART
            serial.port = &Serial1;
        #else
            Serial2.begin(Cfg.serBaud, SERIAL_8N1, Cfg.serRx, Cfg.serTx);
            serial.port = &Serial2;
        #endif
    #else
        // UART0 (Serial1) o UART1 (Serial2) segun los pines elegidos
        if (Serial1.setRX(Cfg.serRx) && Serial1.setTX(Cfg.serTx)) { Serial1.begin(Cfg.serBaud); serial.port = &Serial1; }
        else if (Serial2.setRX(Cfg.serRx) && Serial2.setTX(Cfg.serTx)) { Serial2.begin(Cfg.serBaud); serial.port = &Serial2; }
        else logf("serial: pines RX %d / TX %d no valen para ninguna UART\n", Cfg.serRx, Cfg.serTx);
    #endif
    }

    // =========================================================================================================================
    static uint32_t lastUpdate = 0;

    // Proteccion por viento: todo pasa por el envoltorio, que rechaza movimientos mientras esta aparcado
    static RC::WindGuard   windG;
    static RC::WindRotor   windR(windG);
    RC::WindGuard& wind() { return windG; }
    void windConfig() {
        windG.cfg.on = Cfg.windOn; windG.cfg.parkMs = Cfg.windParkMs; windG.cfg.resumeMs = Cfg.windResumeMs; windG.cfg.holdMin = Cfg.windHoldMin;
        windG.local = Cfg.anemoPin >= 0;
    }

    // Anemómetro de cazoletas: la interrupción cuenta los cierres de la ampolla (con filtro de rebotes) y cada segundo el
    // viento (media de 2 min) y la racha (mayor media de 3 s de los últimos 2 min) van a la protección por viento
#ifndef IRAM_ATTR
    #define IRAM_ATTR
#endif
    static RC::Anemometer   anemo;
    static volatile uint32_t anemoPulses = 0, anemoLastUs = 0;
    static uint32_t anemoLastFeed = 0;
    // Rebotes de la ampolla: un cierre cuenta si han pasado 5 ms desde el anterior (200 Hz como mucho)
    static void IRAM_ATTR anemoIsr() {
        const uint32_t t = (uint32_t)micros();
        if (t - anemoLastUs >= 5000) { anemoLastUs = t; anemoPulses = anemoPulses + 1; }
    }

    static void anemoBegin() {
        if (Cfg.anemoPin < 0) return;
        anemo.kmhPerHz = Cfg.anemoKmhHz;
        pinMode(Cfg.anemoPin, Cfg.anemoPullup ? INPUT_PULLUP : INPUT);
        attachInterrupt(digitalPinToInterrupt(Cfg.anemoPin), anemoIsr, FALLING);
        logf("anemometro en el pin %d (%.2f km/h por Hz)\n", Cfg.anemoPin, Cfg.anemoKmhHz);
    }

    static void anemoLoop(uint32_t now) {
        if (Cfg.anemoPin < 0) return;
        anemo.sample(anemoPulses, now);
        if (!anemo.have() || now - anemoLastFeed < 1000) return;
        anemoLastFeed = now;
        if (windG.update(anemo.avgMs(120), anemo.gustMs(120), -1, 0, "anemo", now)) { rotor().stop(); rotor().park(); }
    }
    static RC::Rotor& realRotor() { if (Cfg.driver == 1) return serial; return direct; }

    void begin() {
        windConfig();
        anemoBegin();
        if (Cfg.driver == 1) {
            openSerial();
            return;
        }
        direct.az.cfg = Cfg.az;
        direct.el.cfg = Cfg.el;
        direct.el.cfg.enabled = Cfg.elEnabled;
        direct.az.cfg.sensorless = Cfg.azPins.sensor == 2;
        direct.el.cfg.sensorless = Cfg.elPins.sensor == 2;
        direct.parkAz = Cfg.parkAz;
        direct.parkEl = Cfg.parkEl;
        direct.reportMech = Cfg.reportMech;
        setupAxisPins(Cfg.azPins);
        if (Cfg.elEnabled) setupAxisPins(Cfg.elPins);
        if (Cfg.brakePin >= 0) { outPin(Cfg.brakePin, false); Board::pinOutput(Cfg.brakePin); outPin(Cfg.brakePin, false); }
        usingAds = Cfg.azPins.sensor == 1 || (Cfg.elEnabled && Cfg.elPins.sensor == 1);
        Ads::begin(usingAds);
    }

    void allOff() {
        if (Cfg.driver == 1) return;
        RC::AxisOut off;
        applyAxis(Cfg.azPins, off);
        if (Cfg.elEnabled) applyAxis(Cfg.elPins, off);
        outPin(Cfg.brakePin, false);
    }

    void loop() {
        windG.tick(millis());
        anemoLoop(millis());
        if (Cfg.driver == 1) { serial.poll(); return; }
        if (usingAds) Ads::poll(Cfg.azPins.sensor == 1 ? Cfg.azPins.adsChannel : -1, (Cfg.elEnabled && Cfg.elPins.sensor == 1) ? Cfg.elPins.adsChannel : -1);
        const uint32_t now = millis();
        if (now - lastUpdate < 20) return;
        lastUpdate = now;
        rawAz = readSensor(Cfg.azPins, usingAds);
        rawEl = Cfg.elEnabled ? readSensor(Cfg.elPins, usingAds) : NAN;
        direct.update(now, rawAz, rawEl);
        applyAxis(Cfg.azPins, direct.az.out());
        if (Cfg.elEnabled) applyAxis(Cfg.elPins, direct.el.out());
        outPin(Cfg.brakePin, direct.az.out().brakeRelease || (Cfg.elEnabled && direct.el.out().brakeRelease));
    }

    RC::Rotor& rotor() { windR.attach(&realRotor()); return windR; }

    const char* name() {
        if (Cfg.driver == 1) return RC::serialProtoName(Cfg.serProto);
        return Cfg.outMode == 1 ? "direct (H-bridge)" : "direct (relays)";
    }

    uint8_t fault() {
        uint8_t f;
        if (Cfg.driver == 1) {
            f = !serial.linkOk() ? RC::F_LINK : (Cfg.serProto == RC::SP_RSBUS ? serial.busFault() : RC::F_NONE);
        } else {
            f = direct.fault();
        }
        return f == RC::F_NONE && windG.locked() ? RC::F_WIND : f;
    }

    void raw(JsonObject o) {
        if (Cfg.anemoPin >= 0) {                                    // para calibrar el anemómetro: impulsos por segundo
            o["anemoHz"] = round(anemo.hz(3) * 100) / 100;
            o["anemoPulses"] = (uint32_t)anemoPulses;
        }
        if (Cfg.driver == 1) {
            o["link"] = serial.linkOk();
            if (Cfg.serProto == RC::SP_RSBUS && serial.busErr) o["err"] = serial.busErr;   // código del controlador
            return;
        }
        if (!isnan(rawAz)) o["az"] = rawAz;
        if (!isnan(rawEl)) o["el"] = rawEl;
        o["unit"] = (Cfg.azPins.sensor == 1) ? "ads" : (Board::adcIsCounts(Cfg.azPins.adcPin) ? "adc" : "mV");
    }

    void posExtra(JsonObject o) {
        o["f"] = RC::faultText(fault());
        if (Cfg.driver == 1) {
            if (!isnan(serial.taz)) o["taz"] = serial.taz;
            if (!isnan(serial.tel) && Cfg.serHasEl) o["tel"] = serial.tel;
            return;
        }
        o["maz"] = round(direct.az.pos() * 10) / 10;
        if (!isnan(direct.az.target())) o["taz"] = round(direct.az.target() * 10) / 10;
        if (Cfg.elEnabled && !isnan(direct.el.target())) o["tel"] = round(direct.el.target() * 10) / 10;
    }

    static RC::Axis* axisOf(const char* a, RC::AxisCfg** cfg) {
        if (strcmp(a, "el") == 0) { *cfg = &Cfg.el; return Cfg.elEnabled ? &direct.el : nullptr; }
        *cfg = &Cfg.az;
        return &direct.az;
    }

    bool calibrate(const char* axis, const char* pt, double deg, std::string& err) {
        if (Cfg.driver != 0) { err = "only direct drive"; return false; }
        RC::AxisCfg* c;
        RC::Axis* ax = axisOf(axis, &c);
        if (!ax) { err = "no el axis"; return false; }
        const double r = strcmp(axis, "el") == 0 ? rawEl : rawAz;
        if (isnan(r)) { err = "no sensor reading"; return false; }
        if (strcmp(pt, "b") == 0) { c->rawB = r; c->degB = deg; }
        else { c->rawA = r; c->degA = deg; }
        ax->cfg.rawA = c->rawA; ax->cfg.degA = c->degA; ax->cfg.rawB = c->rawB; ax->cfg.degB = c->degB;
        if (!Config::save()) { err = "save failed"; return false; }
        return true;
    }

    bool setHere(const char* axis, double deg, std::string& err) {
        if (Cfg.driver != 0) { err = "only direct drive"; return false; }
        RC::AxisCfg* c;
        RC::Axis* ax = axisOf(axis, &c);
        if (!ax || !ax->havePos()) { err = "no position"; return false; }
        const double delta = deg - ax->pos();
        c->degA += delta; c->degB += delta;
        ax->cfg.degA = c->degA; ax->cfg.degB = c->degB;
        if (!Config::save()) { err = "save failed"; return false; }
        return true;
    }

    void clearFault() {
        windG.release();
        if (Cfg.driver == 0) direct.clearFaults();
        else if (Cfg.serProto == RC::SP_RSBUS) serial.busClear();
    }
}
