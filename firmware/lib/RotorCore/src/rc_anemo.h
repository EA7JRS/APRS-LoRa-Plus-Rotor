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
// Anemómetro de cazoletas con ampolla reed (un impulso cada vez que el imán pasa): la placa cuenta los impulsos y aquí se
// convierten en viento. kmhPerHz = km/h que corresponden a un impulso por segundo (2,4 en los de las estaciones baratas tipo
// Misol / Fine Offset; Davis 6410: 3,62). Viento = media de los últimos 3 s; medio = media de 2 o 10 min; racha = la mayor
// media de 3 s del periodo (como la OMM). Sin dependencias de Arduino (el mismo código en la pasarela NMEA 2000).
#include <math.h>
#include <stdint.h>

namespace RC {

    class Anemometer {
    public:
        static const int SLOTS = 600;               // 10 min, un hueco por segundo
        double kmhPerHz = 2.4;

        // Llamar a menudo con el total de impulsos contados (sube siempre; da la vuelta en 2^32 sin problema)
        void sample(uint32_t total, uint32_t nowMs) {
            if (!started_) { started_ = true; lastTotal_ = total; lastMs_ = nowMs; return; }
            const uint32_t el = nowMs - lastMs_;
            if (el < 1000) return;
            if (el > 5000) { lastTotal_ = total; lastMs_ = nowMs; filled_ = 0; return; }   // hueco largo: se empieza de nuevo
            const double hz = (double)(total - lastTotal_) * 1000.0 / el;
            lastTotal_ = total;
            lastMs_ = nowMs;
            head_ = (head_ + 1) % SLOTS;
            hz_[head_] = (float)hz;
            if (filled_ < SLOTS) filled_++;
        }

        bool   have() const { return filled_ >= 3; }
        int    seconds() const { return filled_; }
        // Frecuencia de los últimos n segundos
        double hz(int n = 3) const {
            if (filled_ == 0) return 0;
            if (n > filled_) n = filled_;
            double s = 0;
            for (int i = 0; i < n; i++) s += hz_[(head_ - i + SLOTS) % SLOTS];
            return s / n;
        }
        double toMs(double hz) const { return hz * kmhPerHz / 3.6; }
        double windMs() const { return toMs(hz(3)); }
        double avgMs(int seconds = 120) const { return toMs(hz(seconds)); }
        // Mayor media de 3 s de los últimos 'seconds'
        double gustMs(int seconds = 600) const {
            if (filled_ < 3) return windMs();
            if (seconds > filled_) seconds = filled_;
            double best = 0;
            for (int i = 0; i + 3 <= seconds; i++) {
                const double m = (hz_[(head_ - i + SLOTS) % SLOTS] + hz_[(head_ - i - 1 + SLOTS) % SLOTS] + hz_[(head_ - i - 2 + SLOTS) % SLOTS]) / 3.0;
                if (m > best) best = m;
            }
            return toMs(best);
        }
        // Escala de Beaufort (0-12) de una velocidad en m/s
        static int beaufort(double ms) {
            static const double lim[12] = {0.3, 1.6, 3.4, 5.5, 8.0, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7};
            int b = 0;
            while (b < 12 && ms >= lim[b]) b++;
            return b;
        }

    private:
        float    hz_[SLOTS] = {0};
        int      head_ = 0, filled_ = 0;
        bool     started_ = false;
        uint32_t lastTotal_ = 0, lastMs_ = 0;
    };

    // Filtro de rebotes de la ampolla: un impulso solo cuenta si han pasado minUs desde el anterior (lo usa la interrupción)
    struct ReedDebounce {
        uint32_t minUs = 5000;                      // 200 Hz como mucho (480 km/h con 2,4 km/h por Hz)
        uint32_t last = 0;
        bool accept(uint32_t nowUs) {
            if (nowUs - last < minUs) return false;
            last = nowUs;
            return true;
        }
    };
}
