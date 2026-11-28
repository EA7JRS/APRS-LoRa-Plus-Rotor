/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */
#pragma once
// Compensacion de actitud para el rotor en una plataforma movil (coche, barco) o cuando el mastil se gira con el viento.
// En la cadena por dentro del rumbo: clientes -> SkyRotor -> HeadingRotor (guinada / rumbo) -> AttitudeRotor (escora,
// cabeceo, giro del mastil) -> WindRotor -> rotor. Dada la actitud de la base (roll = escora, pitch = cabeceo, yaw = giro
// del mastil medido por el giroscopo), transforma el az/el del objetivo (nivelado, referido a la proa) al marco inclinado
// del rotor, para que el eje de la antena apunte al objetivo. Vale para 2 y 3 ejes (el 3.er eje estabiliza mecanicamente;
// esto afina lo que no llegue). Los signos de cada eje calibran la IMU; la tara ("att zero") fija el cero del montaje
// (parte de escora y cabeceo 0). Con roll = pitch = yaw = 0 devuelve lo mismo (sin cambios). Porte de attitude.py (Pi).
#include <math.h>
#include <stdint.h>
#include "rc_common.h"

namespace RC {

    class AttitudeRotor : public Rotor {
    public:
        bool     enabled = false;
        double   stepDeg = 0.5;                 // cambio de actitud que obliga a volver a apuntar
        uint32_t staleMs = 5000;                // sin actitud nueva en este tiempo: stale()
        int      rollSign = 1, pitchSign = 1, yawSign = 1;
        double   rollZero = 0, pitchZero = 0, yawZero = 0;      // cero del montaje (tara)

        void attach(Rotor* r) { r_ = r; }
        Rotor& inner() { return *r_; }

        // Objetivo nivelado (az referido a la proa, el sobre el horizonte) -> az/el que debe mandar el rotor en la base inclinada.
        static void aim(double az, double el, double roll, double pitch, double yaw, double& azm, double& elm) {
            Vec3 v = vecOf(az, el);
            v = rotZ(v, -yaw * kRad);
            v = rotX(v, -pitch * kRad);
            v = rotY(v, -roll * kRad);
            azel(v, azm, elm);
        }
        // Inverso de aim: del az/el del rotor (base inclinada) al az/el nivelado (para informar la posicion real).
        static void unaim(double azm, double elm, double roll, double pitch, double yaw, double& az, double& el) {
            Vec3 v = vecOf(azm, elm);
            v = rotY(v, roll * kRad);
            v = rotX(v, pitch * kRad);
            v = rotZ(v, yaw * kRad);
            azel(v, az, el);
        }

        // Actitud nueva (grados). Llamar con cada dato de la IMU.
        void setAttitude(double roll, double pitch, double yaw, uint32_t nowMs) {
            rawR_ = rollSign * roll; rawP_ = pitchSign * pitch; rawY_ = yawSign * yaw;
            applyZero();
            attMs_ = nowMs; have_ = true;
            tick(nowMs);
        }
        // Pone a cero escora, cabeceo y giro: la actitud cruda actual pasa a ser la referencia. Devuelve los ceros guardados.
        void tare(double& r, double& p, double& y) {
            rollZero = rawR_; pitchZero = rawP_; yawZero = rawY_;
            applyZero();
            if (target_) aimNow(0, true);
            r = rollZero; p = pitchZero; y = yawZero;
        }
        void setZero(double r, double p, double y) {
            rollZero = r; pitchZero = p; yawZero = y;
            applyZero();
            if (target_) aimNow(0, true);
        }

        void tick(uint32_t nowMs) {
            if (!active() || !target_ || nowMs - aimMs_ < 500) return;
            if (fabs(angleDiff(aimR_, roll_)) >= stepDeg || fabs(angleDiff(aimP_, pitch_)) >= stepDeg ||
                fabs(angleDiff(aimY_, yaw_)) >= stepDeg)
                aimNow(nowMs);
        }
        bool     active() const { return enabled && have_ && r_; }
        bool     have() const { return have_; }
        double   roll() const { return roll_; }
        double   pitch() const { return pitch_; }
        double   yaw() const { return yaw_; }
        bool     stale(uint32_t nowMs) const { return have_ && nowMs - attMs_ > staleMs; }
        uint32_t ageS(uint32_t nowMs) const { return have_ ? (nowMs - attMs_) / 1000u : 0; }

        bool setPos(double a, double e) override {
            if (!active()) return r_->setPos(a, e);
            target_ = true; tAz_ = a; tEl_ = e; withEl_ = true;
            return aimNow(0, true);
        }
        bool setAz(double a) override {
            if (!active()) return r_->setAz(a);
            target_ = true; tAz_ = a; withEl_ = false;
            return aimNow(0, true);
        }
        void getPos(double& a, double& e) override {
            r_->getPos(a, e);
            if (active()) { double na, ne; unaim(a, e, roll_, pitch_, yaw_, na, ne); a = na; e = ne; }
        }
        bool stop() override { target_ = false; return r_->stop(); }
        bool stopAz() override { target_ = false; return r_->stopAz(); }
        bool stopEl() override { return r_->stopEl(); }
        bool park() override { target_ = false; return r_->park(); }
        bool move(int d, int s) override { target_ = false; return r_->move(d, s); }
        uint32_t status() override { return r_->status(); }
        const char* info() override { return r_->info(); }
        double minAz() const override { return active() ? 0 : r_->minAz(); }
        double maxAz() const override { return active() ? 360 : r_->maxAz(); }
        double minEl() const override { return r_->minEl(); }
        double maxEl() const override { return r_->maxEl(); }
        bool   hasEl() const override { return r_->hasEl(); }

    private:
        static constexpr double kRad = 3.14159265358979323846 / 180.0;
        struct Vec3 { double x, y, z; };         // x estribor, y proa, z arriba
        static Vec3 rotZ(Vec3 v, double a) { double c = cos(a), s = sin(a); return { v.x * c - v.y * s, v.x * s + v.y * c, v.z }; }
        static Vec3 rotX(Vec3 v, double a) { double c = cos(a), s = sin(a); return { v.x, v.y * c - v.z * s, v.y * s + v.z * c }; }
        static Vec3 rotY(Vec3 v, double a) { double c = cos(a), s = sin(a); return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c }; }
        static Vec3 vecOf(double az, double el) {
            const double ce = cos(el * kRad);
            return { ce * sin(az * kRad), ce * cos(az * kRad), sin(el * kRad) };
        }
        static void azel(Vec3 v, double& az, double& el) {
            az = atan2(v.x, v.y) / kRad; if (az < 0) az += 360.0;
            const double z = v.z < -1 ? -1 : (v.z > 1 ? 1 : v.z);
            el = asin(z) / kRad;
        }

        Rotor*   r_ = nullptr;
        bool     have_ = false, target_ = false, withEl_ = false;
        double   rawR_ = 0, rawP_ = 0, rawY_ = 0;            // crudo con signo, antes de la tara
        double   roll_ = 0, pitch_ = 0, yaw_ = 0;            // efectivo (crudo - tara)
        double   aimR_ = 0, aimP_ = 0, aimY_ = 0;            // actitud en el ultimo apuntado
        double   tAz_ = 0, tEl_ = 0;
        uint32_t attMs_ = 0, aimMs_ = 0;

        void applyZero() { roll_ = rawR_ - rollZero; pitch_ = rawP_ - pitchZero; yaw_ = rawY_ - yawZero; }

        bool aimNow(uint32_t nowMs, bool first = false) {
            aimR_ = roll_; aimP_ = pitch_; aimY_ = yaw_;
            aimMs_ = first ? attMs_ : nowMs;
            double am, em;
            if (!withEl_) {
                aim(tAz_, 0.0, roll_, pitch_, yaw_, am, em);
                const bool ok = r_->setAz(am);
                if (!ok) target_ = false;
                return ok;
            }
            aim(tAz_, tEl_, roll_, pitch_, yaw_, am, em);
            if (em < r_->minEl()) em = r_->minEl();
            if (em > r_->maxEl()) em = r_->maxEl();
            const bool ok = r_->setPos(am, em);
            if (!ok) target_ = false;
            return ok;
        }
    };
}
