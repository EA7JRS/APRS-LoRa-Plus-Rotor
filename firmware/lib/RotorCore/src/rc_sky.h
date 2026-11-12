/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */
#pragma once
// Seguir el sol o la luna (ruido solar para medir la antena, rebote lunar EME). SkyRotor va por fuera de todo:
// clientes -> SkyRotor -> HeadingRotor -> WindRotor -> rotor de verdad. Cualquier orden de un cliente (pos, stop, park,
// move) deja de seguir. Cada "periodMs" calcula la posicion con las formulas del nucleo del plotter (astro.h: Meeus,
// luna topocentrica con paralaje, ~0,03 grados) y, si se ha movido "stepDeg" o mas, apunta. Con "refraction" suma la
// refraccion media (lo que ve la antena). Por debajo de "lowEl" espera (no mueve) hasta que vuelva a salir.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "rc_common.h"
#include "rc_heading.h"
#include "astro.h"

namespace RC {
    enum SkyTarget : uint8_t { SKY_OFF = 0, SKY_SUN = 1, SKY_MOON = 2 };

    inline const char* skyName(int t) { return t == SKY_SUN ? "sun" : t == SKY_MOON ? "moon" : "off"; }
    inline int skyParse(const char* s) {
        if (!s) return -1;
        if (!strcmp(s, "sun")) return SKY_SUN;
        if (!strcmp(s, "moon")) return SKY_MOON;
        if (!strcmp(s, "off")) return SKY_OFF;
        return -1;
    }

    // Altura y azimut del sol o la luna (grados) en un instante Unix
    inline bool skyPosition(int target, double lat, double lon, int64_t unixS, bool refraction, double& az, double& el) {
        if (target != SKY_SUN && target != SKY_MOON) return false;
        int y, m, d;
        double h, dist;
        plotter::unixToUtc((long long)unixS, y, m, d, h);
        if (target == SKY_SUN) plotter::sunPosition(y, m, d, h, lat, lon, el, az);
        else plotter::moonPosition(y, m, d, h, lat, lon, el, az, dist);
        if (refraction) el += plotter::refractionDeg(el);
        return true;
    }

    class SkyRotor : public Rotor {
    public:
        double   stepDeg = 1.0;                 // cambio minimo (acimut o elevacion) para volver a apuntar
        double   lowEl = 0.0;                   // por debajo de esta elevacion: espera
        bool     refraction = true;
        uint32_t periodMs = 2000;

        void attach(Rotor* r) { r_ = r; }

        // Empieza a seguir (false sin posicion de la estacion o sin hora); off con SKY_OFF
        bool start(int target, double lat, double lon, bool havePos, int64_t unixS, uint32_t nowMs) {
            if (target == SKY_OFF) { stopTracking(); return true; }
            if (!havePos || unixS < 1704067200 || (target != SKY_SUN && target != SKY_MOON)) return false;
            target_ = target; lat_ = lat; lon_ = lon;
            lastMs_ = nowMs - periodMs; aimed_ = false;
            tick(unixS, nowMs);
            return true;
        }
        void stopTracking() { target_ = SKY_OFF; below_ = false; }
        void setStation(double lat, double lon) { lat_ = lat; lon_ = lon; }

        // Llamar a menudo con la hora Unix (UTC) y millis()
        void tick(int64_t unixS, uint32_t nowMs) {
            if (target_ == SKY_OFF || !r_ || nowMs - lastMs_ < periodMs) return;
            lastMs_ = nowMs;
            double az, el;
            if (!skyPosition(target_, lat_, lon_, unixS, refraction, az, el)) return;
            az_ = az; el_ = el;
            below_ = el < lowEl;
            if (below_) return;
            if (aimed_ && angDiff(az, aimAz_) < stepDeg && fabs(el - aimEl_) < stepDeg) return;
            double e = el;
            if (r_->hasEl()) {
                if (e < r_->minEl()) e = r_->minEl();
                if (e > r_->maxEl()) e = r_->maxEl();
            }
            const bool ok = r_->hasEl() ? r_->setPos(az, e) : r_->setAz(az);
            if (ok) { aimed_ = true; aimAz_ = az; aimEl_ = el; aims_++; }
        }

        int      target() const { return target_; }
        bool     tracking() const { return target_ != SKY_OFF; }
        bool     below() const { return below_; }
        double   az() const { return az_; }
        double   el() const { return el_; }
        uint32_t aims() const { return aims_; }

        // Ordenes de los clientes: dejan de seguir
        bool setPos(double a, double e) override { stopTracking(); return r_->setPos(a, e); }
        bool setAz(double a) override { stopTracking(); return r_->setAz(a); }
        bool stop() override { stopTracking(); return r_->stop(); }
        bool stopAz() override { stopTracking(); return r_->stopAz(); }
        bool stopEl() override { return r_->stopEl(); }
        bool park() override { stopTracking(); return r_->park(); }
        bool move(int d, int s) override { stopTracking(); return r_->move(d, s); }
        void getPos(double& a, double& e) override { r_->getPos(a, e); }
        uint32_t status() override { return r_->status(); }
        const char* info() override { return r_->info(); }
        double minAz() const override { return r_->minAz(); }
        double maxAz() const override { return r_->maxAz(); }
        double minEl() const override { return r_->minEl(); }
        double maxEl() const override { return r_->maxEl(); }
        bool hasEl() const override { return r_->hasEl(); }

    private:
        Rotor*   r_ = nullptr;
        int      target_ = SKY_OFF;
        double   lat_ = 0, lon_ = 0, az_ = 0, el_ = 0, aimAz_ = 0, aimEl_ = 0;
        bool     below_ = false, aimed_ = false;
        uint32_t lastMs_ = 0, aims_ = 0;
    };
}
