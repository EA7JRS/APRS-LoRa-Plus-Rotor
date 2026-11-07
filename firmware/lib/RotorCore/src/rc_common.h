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
// Tipos comunes del nucleo (C++ puro, sin Arduino: se compila y se prueba en el PC).
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <string>

namespace RC {

    // Banderas de estado: mismos valores que rot_status_t de Hamlib 4.6+ (orden "s" / \get_status de rotctld)
    enum : uint32_t {
        ST_BUSY         = 1u << 0,
        ST_MOVING       = 1u << 1,
        ST_MOVING_AZ    = 1u << 2,
        ST_MOVING_LEFT  = 1u << 3,
        ST_MOVING_RIGHT = 1u << 4,
        ST_MOVING_EL    = 1u << 5,
        ST_MOVING_UP    = 1u << 6,
        ST_MOVING_DOWN  = 1u << 7,
        ST_LIMIT_UP     = 1u << 8,
        ST_LIMIT_DOWN   = 1u << 9,
        ST_LIMIT_LEFT   = 1u << 10,
        ST_LIMIT_RIGHT  = 1u << 11,
        ST_OVERLAP_UP   = 1u << 12,
        ST_OVERLAP_DOWN = 1u << 13,
        ST_OVERLAP_LEFT = 1u << 14,
        ST_OVERLAP_RIGHT= 1u << 16,
    };

    // Direcciones de la orden "M" de rotctld (ROT_MOVE_* de Hamlib)
    enum : int {
        MOVE_UP    = 1 << 1,
        MOVE_DOWN  = 1 << 2,
        MOVE_LEFT  = 1 << 3,     // CCW
        MOVE_RIGHT = 1 << 4,     // CW
    };

    // Codigos de error de Hamlib (rotctld responde "RPRT -n")
    enum { E_OK = 0, E_INVAL = 1, E_NIMPL = 4, E_IO = 6, E_PROTO = 8, E_REJECTED = 9 };

    // Fallo grave del rotor (lo pone el control directo o la pasarela serie)
    enum Fault : uint8_t { F_NONE = 0, F_STALL_AZ, F_STALL_EL, F_SENSOR, F_LINK, F_ESTOP };

    inline const char* faultText(uint8_t f) {
        switch (f) {
            case F_NONE:     return "OK";
            case F_STALL_AZ: return "STALL AZ";
            case F_STALL_EL: return "STALL EL";
            case F_SENSOR:   return "SENSOR";
            case F_LINK:     return "NO LINK";
            case F_ESTOP:    return "E-STOP";
        }
        return "?";
    }

    inline double wrap360(double a) { double x = fmod(a, 360.0); if (x < 0) x += 360.0; return x; }

    // Diferencia con signo de a hasta b (-180, 180]
    inline double angleDiff(double a, double b) {
        double d = fmod(b - a, 360.0);
        if (d > 180.0) d -= 360.0;
        if (d <= -180.0) d += 360.0;
        return d;
    }

    // Lo que ofrece un rotor a los servidores (rotctld, GS-232, Rot2Prog, EasyComm, BLE, APRS): lo implementa el firmware
    // con el control directo o con la pasarela serie, y en las pruebas un rotor simulado.
    class Rotor {
    public:
        virtual ~Rotor() {}
        virtual bool        setPos(double azDeg, double elDeg) = 0;     // az 0-360 (o el rango mecanico), el 0-180; false = rechazada
        virtual bool        setAz(double azDeg) = 0;                    // solo acimut (GS-232 "M")
        virtual void        getPos(double& azDeg, double& elDeg) = 0;   // posicion real (si no se conoce, la ultima pedida)
        virtual bool        stop() = 0;
        virtual bool        stopAz() { return stop(); }
        virtual bool        stopEl() { return stop(); }
        virtual bool        park() = 0;
        virtual bool        move(int dir, int speedPct) = 0;            // MOVE_*; speedPct 1-100 (0 = la configurada)
        virtual uint32_t    status() = 0;                               // banderas ST_*
        virtual const char* info() = 0;                                 // texto de una linea
        virtual double      minAz() const { return 0; }
        virtual double      maxAz() const { return 360; }
        virtual double      minEl() const { return 0; }
        virtual double      maxEl() const { return 90; }
        virtual bool        hasEl() const { return true; }
    };

    inline bool number(const std::string& s, double& v) {
        if (s.empty()) return false;
        char* end = nullptr;
        v = strtod(s.c_str(), &end);
        return end && *end == 0 && isfinite(v);
    }

    inline size_t split(const std::string& s, std::string* tok, size_t max) {
        size_t n = 0, i = 0;
        while (i < s.size() && n < max) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
            if (i >= s.size()) break;
            size_t j = i;
            while (j < s.size() && s[j] != ' ' && s[j] != '\t') j++;
            tok[n++] = s.substr(i, j - i);
            i = j;
        }
        return n;
    }

}
