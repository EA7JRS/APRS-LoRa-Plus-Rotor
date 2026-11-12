/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */
#pragma once
// Rotor en un barco o en un coche: el acimut que piden el tracker, la app o Hamlib es verdadero (respecto al norte), pero el rotor
// gira respecto a la proa. HeadingRotor resta el rumbo verdadero del barco al pedir y lo suma al leer, y vuelve a apuntar cuando el
// rumbo cambia "stepDeg" grados o mas (como mucho una vez por segundo). El rumbo llega de la red local (HDT / HDG de NMEA 0183,
// navigation.headingTrue de Signal K; src/lan.cpp). Apagado, o sin haber recibido nunca un rumbo, deja pasar todo tal cual; si el
// rumbo deja de llegar sigue con el ultimo (stale() lo dice).
// Va por fuera de WindRotor: tracker -> HeadingRotor -> WindRotor -> rotor de verdad.
#include <math.h>
#include <stdint.h>
#include "rc_common.h"

namespace RC {
    inline double norm360(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360.0 : a; }
    inline double angDiff(double a, double b) { double d = norm360(a - b); return d > 180 ? 360 - d : d; }   // 0-180

    class HeadingRotor : public Rotor {
    public:
        bool     enabled = false;
        double   stepDeg = 3.0;                 // cambio de rumbo que obliga a volver a apuntar
        uint32_t staleMs = 10000;               // sin rumbo nuevo en este tiempo: stale()

        void attach(Rotor* r) { r_ = r; }
        Rotor& inner() { return *r_; }

        // Rumbo verdadero nuevo (grados). Llamar con cada dato y, si no hay datos, con tick() de vez en cuando.
        void setHeading(double deg, uint32_t nowMs) {
            hdg_ = norm360(deg); hdgMs_ = nowMs; have_ = true;
            tick(nowMs);
        }
        void tick(uint32_t nowMs) {
            if (!active() || !target_ || nowMs - aimMs_ < 1000) return;
            if (angDiff(hdg_, aimHdg_) >= stepDeg) aim(nowMs);
        }
        bool   active() const { return enabled && have_ && r_; }
        bool   have() const { return have_; }
        double heading() const { return hdg_; }
        bool   stale(uint32_t nowMs) const { return have_ && nowMs - hdgMs_ > staleMs; }
        uint32_t ageS(uint32_t nowMs) const { return have_ ? (nowMs - hdgMs_) / 1000u : 0; }

        // Acimut del rotor (respecto a la proa) para un acimut verdadero: de las vueltas posibles, la mas cercana a la posicion actual
        double relative(double trueAz) {
            const double r = norm360(trueAz - hdg_);
            double cur, e;
            r_->getPos(cur, e);
            double best = NAN;
            for (int k = -1; k <= 1; k++) {
                const double c = r + 360.0 * k;
                if (c < r_->minAz() - 1e-6 || c > r_->maxAz() + 1e-6) continue;
                if (isnan(best) || fabs(c - cur) < fabs(best - cur)) best = c;
            }
            return isnan(best) ? r : best;      // fuera del recorrido: que lo rechace el rotor
        }

        bool setPos(double a, double e) override {
            if (!active()) return r_->setPos(a, e);
            target_ = true; tAz_ = a; tEl_ = e; withEl_ = true;
            return aim(0, true);
        }
        bool setAz(double a) override {
            if (!active()) return r_->setAz(a);
            target_ = true; tAz_ = a; withEl_ = false;
            return aim(0, true);
        }
        void getPos(double& a, double& e) override {
            r_->getPos(a, e);
            if (active()) a = norm360(a + hdg_);
        }
        bool stop() override { target_ = false; return r_->stop(); }
        bool stopAz() override { target_ = false; return r_->stopAz(); }
        bool stopEl() override { return r_->stopEl(); }
        bool park() override { target_ = false; return r_->park(); }       // reposo mecanico (respecto a la proa)
        bool move(int d, int s) override { target_ = false; return r_->move(d, s); }
        uint32_t status() override { return r_->status(); }
        const char* info() override { return r_->info(); }
        // Compensando, el acimut que se pide es verdadero: 0-360
        double minAz() const override { return active() ? 0 : r_->minAz(); }
        double maxAz() const override { return active() ? 360 : r_->maxAz(); }
        double minEl() const override { return r_->minEl(); }
        double maxEl() const override { return r_->maxEl(); }
        bool hasEl() const override { return r_->hasEl(); }

        bool   hasTarget() const { return target_; }
        double targetAz() const { return tAz_; }

    private:
        Rotor*   r_ = nullptr;
        bool     have_ = false, target_ = false, withEl_ = false;
        double   hdg_ = 0, aimHdg_ = 0, tAz_ = 0, tEl_ = 0;
        uint32_t hdgMs_ = 0, aimMs_ = 0;

        bool aim(uint32_t nowMs, bool first = false) {
            aimHdg_ = hdg_;
            aimMs_ = first ? hdgMs_ : nowMs;
            const double rel = relative(tAz_);
            const bool ok = withEl_ ? r_->setPos(rel, tEl_) : r_->setAz(rel);
            if (!ok) target_ = false;            // no se puede (aparcado por viento, fuera del recorrido): deja de seguir
            return ok;
        }
    };
}
