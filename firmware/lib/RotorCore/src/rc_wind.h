/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Proteccion por viento: la app Android manda el tiempo de internet (OpenWeatherMap / IQAir, orden "wx" del protocolo JSON) y,
// con rachas (o viento medio) de "park" m/s o mas, el rotor va a la posicion de reposo y no acepta mas movimientos (rotctld,
// GS-232, BLE, APRS, pantalla...) hasta que el viento baje de "resume" m/s y pasen "hold" minutos sin rachas fuertes. Sin datos
// nuevos se queda aparcado (lo seguro para las antenas); "clear" (borrar fallo) lo libera a mano hasta la siguiente racha fuerte.
// WindRotor envuelve al rotor de verdad: todo lo demas habla con el envoltorio.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rc_common.h"

namespace RC {

    struct WindCfg {
        bool   on = false;
        double parkMs = 15.0;                   // racha (o viento medio si no hay racha) para aparcar
        double resumeMs = 10.0;                 // por debajo de esto, y tras "hold" minutos, vuelve a aceptar movimientos
        int    holdMin = 15;
    };

    class WindGuard {
    public:
        WindCfg cfg;
        bool    local = false;                  // hay anemómetro en la caja: el tiempo de internet (wx) no se usa

        // Dato nuevo (m/s; gust < 0 = sin racha). Devuelve true si acaba de bloquear (hay que aparcar)
        bool update(double wind, double gust, int dirDeg, uint32_t ageS, const char* src, uint32_t nowMs) {
            wind_ = wind; gust_ = gust; dir_ = dirDeg; ageAtRx_ = ageS; rxMs_ = nowMs; have_ = true;
            snprintf(src_, sizeof src_, "%s", src ? src : "");
            const double p = peak();
            if (!cfg.on) { locked_ = false; return false; }
            if (p >= cfg.parkMs) {
                lastStrongMs_ = nowMs;
                released_ = false;
                if (!locked_) { locked_ = true; lockMs_ = nowMs; return true; }
                return false;
            }
            check(nowMs);
            return false;
        }

        // Llamar a menudo: libera cuando toca
        void tick(uint32_t nowMs) { if (!cfg.on) locked_ = false; else check(nowMs); }

        void release() { if (locked_) { locked_ = false; released_ = true; } }   // a mano ("clear")
        bool locked() const { return locked_; }
        bool have() const { return have_; }
        double wind() const { return wind_; }
        double gust() const { return gust_; }
        int dir() const { return dir_; }
        double peak() const { return gust_ >= 0 && gust_ > wind_ ? gust_ : wind_; }
        const char* src() const { return src_; }
        uint32_t ageS(uint32_t nowMs) const { return ageAtRx_ + (nowMs - rxMs_) / 1000u; }
        uint32_t lockedForS(uint32_t nowMs) const { return locked_ ? (nowMs - lockMs_) / 1000u : 0; }
        // segundos hasta poder liberar (0 = ya se podria si el viento esta bajo)
        uint32_t holdLeftS(uint32_t nowMs) const {
            const uint32_t h = (uint32_t)(cfg.holdMin > 0 ? cfg.holdMin : 0) * 60000u, t = nowMs - lastStrongMs_;
            return locked_ && t < h ? (h - t) / 1000u : 0;
        }

    private:
        bool     have_ = false, locked_ = false, released_ = false;
        double   wind_ = 0, gust_ = -1;
        int      dir_ = -1;
        uint32_t rxMs_ = 0, ageAtRx_ = 0, lockMs_ = 0, lastStrongMs_ = 0;
        char     src_[12] = "";

        void check(uint32_t nowMs) {
            if (!locked_) return;
            const uint32_t h = (uint32_t)(cfg.holdMin > 0 ? cfg.holdMin : 0) * 60000u;
            if (peak() < cfg.resumeMs && nowMs - lastStrongMs_ >= h) locked_ = false;
        }
    };

    class WindRotor : public Rotor {
    public:
        explicit WindRotor(WindGuard& g) : g_(g) {}
        void attach(Rotor* r) { r_ = r; }
        Rotor& inner() { return *r_; }

        bool setPos(double a, double e) override { return !g_.locked() && r_->setPos(a, e); }
        bool setAz(double a) override { return !g_.locked() && r_->setAz(a); }
        void getPos(double& a, double& e) override { r_->getPos(a, e); }
        bool stop() override { return r_->stop(); }
        bool stopAz() override { return r_->stopAz(); }
        bool stopEl() override { return r_->stopEl(); }
        bool park() override { return r_->park(); }
        bool move(int d, int s) override { return !g_.locked() && r_->move(d, s); }
        uint32_t status() override { return r_->status(); }
        const char* info() override { return r_->info(); }
        double minAz() const override { return r_->minAz(); }
        double maxAz() const override { return r_->maxAz(); }
        double minEl() const override { return r_->minEl(); }
        double maxEl() const override { return r_->maxEl(); }
        bool hasEl() const override { return r_->hasEl(); }

    private:
        WindGuard& g_;
        Rotor*     r_ = nullptr;
    };

}
