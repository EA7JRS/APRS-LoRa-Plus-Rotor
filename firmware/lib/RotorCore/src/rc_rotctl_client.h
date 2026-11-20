/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Rotor por la red: cliente de un rotctld (Hamlib NET rotctl). Con él una caja con pantalla (LCD-7, 7B...) maneja el rotor de
// otra caja (la Raspberry Pi con el Stepper Motor HAT, puerto 4533) o de cualquier rotctld de Hamlib. Sin red: tick() da lo
// que hay que enviar y feed() recibe lo que llega (el firmware lo pasa por un WiFiClient; las pruebas, por cadenas).
// Una orden cada vez; la parada se adelanta a las que esperan; la última posición pedida manda.
#include <deque>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "rc_common.h"

namespace RC {

    class RotctlClientRotor : public Rotor {
    public:
        // límites hasta que conteste \dump_state (los de serial.* de la configuración)
        double minAz_ = 0, maxAz_ = 450, minEl_ = 0, maxEl_ = 180;
        bool   hasEl_ = true;
        uint32_t pollMs = 500, replyMs = 1500;
        double parkAz = 0, parkEl = 0;               // solo para el texto; el aparcado lo hace la otra caja (K)

        double az = 0, el = 0, taz = NAN, tel = NAN;
        bool   known = false;

        void onConnect(uint32_t now) {
            q_.clear(); rx_.clear(); busy_ = false; dumpDone_ = false;
            push({"\\dump_state\n", K_DUMP, false});
            lastPoll_ = now;
            connected_ = true;
        }
        void onDisconnect() { connected_ = false; busy_ = false; q_.clear(); rx_.clear(); }
        bool connected() const { return connected_; }
        bool linkOk(uint32_t now) const { return connected_ && lastReply_ && now - lastReply_ < 3000; }

        // Lo que hay que mandar ahora ("" = nada)
        void tick(uint32_t now, std::string& out) {
            if (!connected_) return;
            if (busy_ && now - sentAt_ > replyMs) {             // sin respuesta: se da por perdida (o protocolo 0 del dump_state)
                if (kind_ == K_DUMP) dumpDone_ = true;
                busy_ = false;
            }
            if (busy_) return;
            if (q_.empty() && now - lastPoll_ >= pollMs) { lastPoll_ = now; push({"p\n", K_POS, false}); }
            if (q_.empty()) return;
            Item it = q_.front(); q_.pop_front();
            out += it.data;
            kind_ = it.kind; busy_ = true; sentAt_ = now; posLine_ = 0;
        }

        void feed(const char* d, size_t n, uint32_t now) {
            rx_.append(d, n);
            for (;;) {
                const size_t e = rx_.find('\n');
                if (e == std::string::npos) break;
                std::string l = rx_.substr(0, e);
                rx_.erase(0, e + 1);
                while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
                line(l, now);
            }
            if (rx_.size() > 512) rx_.clear();
        }

        // ---- Rotor -------------------------------------------------------------------------------------------------------
        bool setPos(double a, double e) override {
            if (!connected_) return false;
            taz = a; tel = hasEl_ ? e : 0; manual_ = 0;
            drop(K_SET);
            char b[48]; snprintf(b, sizeof b, "P %.1f %.1f\n", a, hasEl_ ? e : 0.0);
            push({b, K_SET, false});
            return true;
        }
        bool setAz(double a) override { return setPos(a, el); }
        void getPos(double& a, double& e) override { a = az; e = hasEl_ ? el : 0; }
        bool stop() override {
            taz = tel = NAN; manual_ = 0;
            if (!connected_) return false;
            drop(K_SET);
            push({"S\n", K_SET, true});
            return true;
        }
        bool park() override {
            if (!connected_) return false;
            taz = tel = NAN; manual_ = 0;
            drop(K_SET);
            push({"K\n", K_SET, false});
            return true;
        }
        bool move(int dir, int pct) override {
            if (!connected_) return false;
            if (!hasEl_ && (dir == MOVE_UP || dir == MOVE_DOWN)) return false;
            if (dir != MOVE_UP && dir != MOVE_DOWN && dir != MOVE_LEFT && dir != MOVE_RIGHT) return false;
            taz = tel = NAN; manual_ = dir;
            drop(K_SET);
            char b[32]; snprintf(b, sizeof b, "M %d %d\n", dir, pct > 0 ? pct : -1);
            push({b, K_SET, false});
            return true;
        }
        uint32_t status() override {
            uint32_t s = 0;
            if (moving_ || manual_) s |= ST_MOVING;
            if (!std::isnan(taz) && std::fabs(angleDiff(az, taz)) > 2.0) s |= ST_BUSY | ST_MOVING_AZ;
            if (hasEl_ && !std::isnan(tel) && std::fabs(tel - el) > 2.0) s |= ST_BUSY | ST_MOVING_EL;
            return s;
        }
        const char* info() override { return "APRS LoRa+ Rotor (rotctld client)"; }
        double minAz() const override { return minAz_; }
        double maxAz() const override { return maxAz_; }
        double minEl() const override { return minEl_; }
        double maxEl() const override { return hasEl_ ? maxEl_ : 0; }
        bool   hasEl() const override { return hasEl_; }
        bool   dumpDone() const { return dumpDone_; }

    private:
        enum Kind { K_POS = 1, K_SET = 2, K_DUMP = 3 };
        struct Item { std::string data; int kind; bool urgent; };
        std::deque<Item> q_;
        std::string rx_;
        bool connected_ = false, busy_ = false, dumpDone_ = false, moving_ = false;
        int kind_ = 0, posLine_ = 0, manual_ = 0;
        uint32_t sentAt_ = 0, lastPoll_ = 0, lastReply_ = 0, lastMove_ = 0;
        double newAz_ = 0;

        void push(const Item& it) {
            if (q_.size() > 8) q_.pop_back();
            if (it.urgent) q_.push_front(it); else q_.push_back(it);
        }
        void drop(int kind) {
            for (auto i = q_.begin(); i != q_.end();) { if (i->kind == kind) i = q_.erase(i); else ++i; }
        }
        void done(uint32_t now) { busy_ = false; lastReply_ = now; }

        void line(const std::string& l, uint32_t now) {
            if (!busy_) return;
            if (l.compare(0, 4, "RPRT") == 0) {                 // fin de cualquier orden (o error de "p")
                if (kind_ == K_DUMP) dumpDone_ = true;
                done(now);
                return;
            }
            if (kind_ == K_DUMP) {
                const size_t eq = l.find('=');
                if (l == "done") { dumpDone_ = true; done(now); return; }
                if (eq == std::string::npos) return;            // "1" (protocolo) y el número de modelo
                const std::string k = l.substr(0, eq);
                const double v = atof(l.c_str() + eq + 1);
                if (k == "min_az") minAz_ = v;
                else if (k == "max_az") maxAz_ = v;
                else if (k == "min_el") minEl_ = v;
                else if (k == "max_el") { maxEl_ = v; hasEl_ = v > 0; }
                return;
            }
            if (kind_ == K_POS) {
                char* end = nullptr;
                const double v = strtod(l.c_str(), &end);
                if (end == l.c_str()) return;
                if (posLine_ == 0) { newAz_ = v; posLine_ = 1; return; }
                double a = newAz_;
                while (a < 0) a += 360.0;                        // rotctld puede dar -180..180
                moving_ = known && (std::fabs(angleDiff(a, az)) > 0.4 || std::fabs(v - el) > 0.4);
                if (moving_) lastMove_ = now;
                else if (now - lastMove_ > 1500 && manual_ == 0) moving_ = false;
                az = a; el = v; known = true;
                if (!std::isnan(taz) && std::fabs(angleDiff(az, taz)) <= 1.0 && (!hasEl_ || std::isnan(tel) || std::fabs(tel - el) <= 1.0))
                    taz = tel = NAN;                            // llegado
                done(now);
                return;
            }
        }
    };
}
