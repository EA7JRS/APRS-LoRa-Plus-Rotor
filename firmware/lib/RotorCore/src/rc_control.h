/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor.
 *
 * APRS LoRa+ Rotor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * APRS LoRa+ Rotor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with APRS LoRa+ Rotor. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
// Control directo de un rotor (motores por reles o puente H, posicion por potenciometro u otro sensor analogico).
// Sin E/S: el firmware le pasa la lectura en bruto de cada eje y aplica la salida (sentido, velocidad 0-1, freno).
// Pensado para Yaesu G-5500 / G-5500DC / G-450 / G-1000DXC / G-2800 (conector DIN del mando: potenciometro y motores),
// Create RC5, Hy-Gain (con freno) y rotores caseros con motor de continua.
#include "rc_common.h"

namespace RC {

    struct AxisCfg {
        bool     enabled        = true;
        double   minDeg         = 0;        // rango mecanico (acimut: 0-360, 0-450, -180-180 con tope al sur...; elevacion 0-90 o 0-180)
        double   maxDeg         = 360;
        double   rawA           = 0;        // calibracion de dos puntos: lectura en bruto -> grados
        double   degA           = 0;
        double   rawB           = 4095;
        double   degB           = 360;
        double   deadband       = 1.0;      // grados: parado si el error es menor
        double   restart        = 2.0;      // grados: vuelve a moverse si el error supera esto (evita que oscile)
        double   slowZone       = 10.0;     // grados: por debajo, la velocidad baja hasta minSpeed
        double   minSpeed       = 0.35;     // 0-1 (PWM). Con reles cualquier valor > 0 es "en marcha"
        double   maxSpeed       = 1.0;
        uint32_t rampMs         = 600;      // arranque suave
        uint32_t reverseDelayMs = 1200;     // pausa antes de invertir el giro (los Yaesu la piden)
        uint32_t stallMs        = 5000;     // atasco: si en este tiempo no avanza stallDeg, para y da fallo (0 = sin vigilancia)
        double   stallDeg       = 1.0;
        uint32_t brakeMs        = 0;        // freno (Hy-Gain): se suelta antes de arrancar y se echa despues de parar (0 = sin freno)
        double   filter         = 0.35;     // filtro de la lectura (0-1; 1 = sin filtro)
        double   limitMargin    = 0.5;      // grados antes de los topes en que se para
        bool     sensorless     = false;    // sin sensor de posicion: solo movimiento manual (con tiempo maximo)
        uint32_t manualMaxMs    = 90000;    // sin sensor: el manual se para solo pasado este tiempo
    };

    struct AxisOut {
        int    dir   = 0;       // -1 (CCW / abajo), 0, +1 (CW / arriba)
        double speed = 0;       // 0-1
        bool   brakeRelease = false;
    };

    class Axis {
    public:
        AxisCfg cfg;

        double toDeg(double raw) const {
            const double d = cfg.rawB - cfg.rawA;
            if (fabs(d) < 1e-9) return cfg.degA;
            return cfg.degA + (raw - cfg.rawA) * (cfg.degB - cfg.degA) / d;
        }
        double toRaw(double deg) const {
            const double d = cfg.degB - cfg.degA;
            if (fabs(d) < 1e-9) return cfg.rawA;
            return cfg.rawA + (deg - cfg.degA) * (cfg.rawB - cfg.rawA) / d;
        }

        void setTarget(double deg) {
            if (cfg.sensorless) return;           // sin sensor no hay posicion a la que ir
            if (deg < cfg.minDeg) deg = cfg.minDeg;
            if (deg > cfg.maxDeg) deg = cfg.maxDeg;
            target_ = deg;
            manual_ = 0;
            if (fault_ == F_NONE) idleArmed_ = true;
        }
        void setManual(int dir, double speed) { manual_ = dir; manualSpeed_ = speed; target_ = NAN; manualAt_ = lastNow_; }
        void stop() { target_ = NAN; manual_ = 0; }
        void clearFault() { fault_ = F_NONE; }
        void setFault(uint8_t f) { fault_ = f; target_ = NAN; manual_ = 0; }

        // Lectura nueva (NAN = sensor sin dato). Devuelve la salida que hay que aplicar.
        AxisOut update(uint32_t now, double raw) {
            AxisOut o;
            lastNow_ = now;
            if (!cfg.enabled) { out_ = o; return o; }
            if (cfg.sensorless) {
                if (manual_ != 0 && now - manualAt_ > cfg.manualMaxMs) manual_ = 0;
            } else if (isnan(raw)) {
                if (fault_ == F_NONE) setFault(F_SENSOR);
            } else {
                const double d = toDeg(raw);
                pos_ = (!havePos_ || cfg.filter >= 1.0) ? d : pos_ + cfg.filter * (d - pos_);
                havePos_ = true;
            }

            // Sentido deseado
            int want = 0;
            double err = 0;
            if (fault_ == F_NONE && (havePos_ || cfg.sensorless)) {
                if (manual_ != 0) {
                    want = manual_;
                } else if (!isnan(target_)) {
                    err = target_ - pos_;
                    if (cur_ != 0) {
                        want = (err * cur_ > cfg.deadband) ? cur_ : 0;     // sigue hasta entrar en la zona muerta (o pasarse)
                        if (want == 0) idleArmed_ = false;
                    } else {
                        const double thr = idleArmed_ ? cfg.deadband : cfg.restart;
                        if (fabs(err) > thr) want = err > 0 ? 1 : -1;
                        else idleArmed_ = false;
                    }
                }
                // Topes
                if (cfg.sensorless) { /* sin posicion: los finales de carrera del propio rotor */ }
                else if (want > 0 && pos_ >= cfg.maxDeg - cfg.limitMargin) { want = 0; atLimit_ = 1; }
                else if (want < 0 && pos_ <= cfg.minDeg + cfg.limitMargin) { want = 0; atLimit_ = -1; }
                else if (want != 0) atLimit_ = 0;
                if (want == 0 && manual_ != 0) manual_ = 0;               // el manual se acaba en el tope
            }

            // Parar / cambiar de sentido
            if (want != cur_) {
                if (cur_ != 0) {
                    lastDir_ = cur_;
                    stoppedAt_ = now;
                    cur_ = 0;
                    brakeHoldUntil_ = now + cfg.brakeMs;
                }
                if (want != 0) {
                    const bool reversing = (lastDir_ != 0 && want != lastDir_);
                    if (!(reversing && now - stoppedAt_ < cfg.reverseDelayMs) && now - stoppedAt_ >= 50) {
                        if (cfg.brakeMs > 0 && !brakeOpen_) {
                            if (!releasing_) { releasing_ = true; releaseAt_ = now; }
                            if (now - releaseAt_ >= cfg.brakeMs) { releasing_ = false; brakeOpen_ = true; }
                        } else {
                            brakeOpen_ = true;
                        }
                        if (brakeOpen_) {
                            cur_ = want;
                            runStart_ = now;
                            stallRef_ = pos_;
                            stallT_ = now;
                        }
                    }
                } else {
                    releasing_ = false;
                }
            }
            if (cur_ == 0 && !releasing_ && brakeOpen_ && (int32_t)(now - brakeHoldUntil_) >= 0) brakeOpen_ = false;

            // Velocidad
            if (cur_ != 0) {
                double sp;
                if (manual_ != 0) sp = manualSpeed_ > 0 ? manualSpeed_ : cfg.maxSpeed;
                else {
                    const double ae = fabs(err);
                    sp = (cfg.slowZone > 0 && ae < cfg.slowZone)
                         ? cfg.minSpeed + (cfg.maxSpeed - cfg.minSpeed) * ae / cfg.slowZone
                         : cfg.maxSpeed;
                }
                if (cfg.rampMs > 0) {
                    const double r = cfg.minSpeed + (cfg.maxSpeed - cfg.minSpeed) * (double)(now - runStart_) / cfg.rampMs;
                    if (sp > r) sp = r;
                }
                if (sp < cfg.minSpeed) sp = cfg.minSpeed;
                if (sp > cfg.maxSpeed) sp = cfg.maxSpeed;
                o.dir = cur_;
                o.speed = sp;
                // Atasco
                if (cfg.stallMs > 0 && !cfg.sensorless) {
                    if (fabs(pos_ - stallRef_) >= cfg.stallDeg) { stallRef_ = pos_; stallT_ = now; }
                    else if (now - stallT_ > cfg.stallMs + cfg.rampMs) {
                        setFault(stallFault_);
                        lastDir_ = cur_;
                        stoppedAt_ = now;
                        cur_ = 0;
                        brakeHoldUntil_ = now + cfg.brakeMs;
                        o.dir = 0;
                        o.speed = 0;
                    }
                }
            }
            o.brakeRelease = brakeOpen_ || releasing_;
            out_ = o;
            return o;
        }

        double  pos() const       { return pos_; }
        bool    havePos() const   { return havePos_; }
        double  target() const    { return target_; }
        int     moving() const    { return cur_; }
        bool    busy() const      { return cur_ != 0 || manual_ != 0 || (!isnan(target_) && fabs(target_ - pos_) > cfg.deadband && fault_ == F_NONE); }
        int     atLimit() const   { return atLimit_; }
        uint8_t fault() const     { return fault_; }
        const AxisOut& out() const { return out_; }
        void    setStallFault(uint8_t f) { stallFault_ = f; }

    private:
        double   pos_ = 0;
        bool     havePos_ = false;
        double   target_ = NAN;
        int      manual_ = 0;
        double   manualSpeed_ = 0;
        int      cur_ = 0;
        int      lastDir_ = 0;
        uint32_t stoppedAt_ = 0;
        uint32_t runStart_ = 0;
        bool     idleArmed_ = false;     // tras una orden nueva arranca con la zona muerta (no con la de rearranque)
        double   stallRef_ = 0;
        uint32_t stallT_ = 0;
        uint8_t  fault_ = F_NONE;
        uint8_t  stallFault_ = F_STALL_AZ;
        int      atLimit_ = 0;
        bool     brakeOpen_ = false;
        bool     releasing_ = false;
        uint32_t releaseAt_ = 0;
        uint32_t brakeHoldUntil_ = 0;
        uint32_t manualAt_ = 0, lastNow_ = 0;
        AxisOut  out_;
    };

    // Rotor de dos ejes con control directo (acimut obligatorio, elevacion opcional)
    class DirectRotor : public Rotor {
    public:
        Axis   az, el;
        double parkAz = 0, parkEl = 0;
        bool   reportMech = false;          // devolver el acimut mecanico (p. ej. 370) en vez de 0-360

        DirectRotor() { el.setStallFault(F_STALL_EL); el.cfg.enabled = false; el.cfg.minDeg = 0; el.cfg.maxDeg = 90; }

        void update(uint32_t now, double rawAz, double rawEl) {
            now_ = now;
            az.update(now, rawAz);
            if (el.cfg.enabled) el.update(now, rawEl);
        }

        // Acimut pedido (0-360 o mecanico) -> acimut mecanico mas cercano dentro del rango
        double mechFor(double azDeg) const {
            if ((azDeg < 0 || azDeg >= 360) && azDeg >= az.cfg.minDeg && azDeg <= az.cfg.maxDeg) return azDeg;
            const double a = wrap360(azDeg);
            const double cur = az.havePos() ? az.pos() : (az.cfg.minDeg + az.cfg.maxDeg) / 2;
            double best = NAN;
            for (int k = -2; k <= 2; k++) {
                const double c = a + 360.0 * k;
                if (c < az.cfg.minDeg - 1e-9 || c > az.cfg.maxDeg + 1e-9) continue;
                if (isnan(best) || fabs(c - cur) < fabs(best - cur)) best = c;
            }
            if (isnan(best)) {   // no cabe (rango < 360): el tope mas cercano
                const double dMin = fabs(angleDiff(a, wrap360(az.cfg.minDeg))), dMax = fabs(angleDiff(a, wrap360(az.cfg.maxDeg)));
                best = dMin < dMax ? az.cfg.minDeg : az.cfg.maxDeg;
            }
            return best;
        }

        bool setPos(double a, double e) override {
            if (az.fault() != F_NONE || (el.cfg.enabled && el.fault() != F_NONE)) clearFaults();
            az.setTarget(mechFor(a));
            if (el.cfg.enabled) el.setTarget(e);
            return true;
        }
        bool setAz(double a) override {
            if (az.fault() != F_NONE) az.clearFault();
            az.setTarget(mechFor(a));
            return true;
        }
        void getPos(double& a, double& e) override {
            a = az.havePos() ? (reportMech ? az.pos() : wrap360(az.pos())) : 0;
            e = el.cfg.enabled && el.havePos() ? el.pos() : 0;
        }
        bool stop() override { az.stop(); el.stop(); return true; }
        bool stopAz() override { az.stop(); return true; }
        bool stopEl() override { el.stop(); return true; }
        bool park() override { return setPos(parkAz, parkEl); }
        bool move(int dir, int pct) override {
            const double sp = pct > 0 ? pct / 100.0 : 0;
            switch (dir) {
                case MOVE_LEFT:  az.clearFault(); az.setManual(-1, sp); return true;
                case MOVE_RIGHT: az.clearFault(); az.setManual(+1, sp); return true;
                case MOVE_UP:    if (!el.cfg.enabled) return false; el.clearFault(); el.setManual(+1, sp); return true;
                case MOVE_DOWN:  if (!el.cfg.enabled) return false; el.clearFault(); el.setManual(-1, sp); return true;
            }
            return false;
        }
        void clearFaults() { az.clearFault(); el.clearFault(); }
        uint8_t fault() const { return az.fault() != F_NONE ? az.fault() : el.fault(); }

        uint32_t status() override {
            uint32_t s = 0;
            if (az.moving()) s |= ST_MOVING | ST_MOVING_AZ | (az.moving() > 0 ? ST_MOVING_RIGHT : ST_MOVING_LEFT);
            if (el.cfg.enabled && el.moving()) s |= ST_MOVING | ST_MOVING_EL | (el.moving() > 0 ? ST_MOVING_UP : ST_MOVING_DOWN);
            if (az.busy() || (el.cfg.enabled && el.busy())) s |= ST_BUSY;
            if (az.atLimit() > 0) s |= ST_LIMIT_RIGHT;
            if (az.atLimit() < 0) s |= ST_LIMIT_LEFT;
            if (el.atLimit() > 0) s |= ST_LIMIT_UP;
            if (el.atLimit() < 0) s |= ST_LIMIT_DOWN;
            if (az.havePos() && az.pos() >= 360.0) s |= ST_OVERLAP_RIGHT;
            if (az.havePos() && az.pos() < 0.0) s |= ST_OVERLAP_LEFT;
            if (el.cfg.enabled && el.havePos() && el.pos() > 90.0) s |= ST_OVERLAP_UP;
            return s;
        }
        const char* info() override { return "APRS LoRa+ Rotor (direct drive)"; }
        double minAz() const override { return az.cfg.minDeg; }
        double maxAz() const override { return az.cfg.maxDeg; }
        double minEl() const override { return el.cfg.enabled ? el.cfg.minDeg : 0; }
        double maxEl() const override { return el.cfg.enabled ? el.cfg.maxDeg : 0; }
        bool   hasEl() const override { return el.cfg.enabled; }

    private:
        uint32_t now_ = 0;
    };

}
