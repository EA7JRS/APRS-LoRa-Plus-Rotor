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
// Servidor del protocolo de red de rotctld (Hamlib 4.x, "NET rotctl" = modelo 2). Lo usan el tracker APRS LoRa+, la app Android,
// GPredict, Look4Sat, PstRotator, SatPC32 (con su puente), Gpredict... Cada conexion TCP tiene su Session.
// Referencia: tests/rotctl_parse.c y rigs/dummy/netrotctl.c de Hamlib (dump_state con protocolo 1: etiquetas "clave=valor" y "done").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "rc_common.h"

namespace RC {

    // Texto de las banderas de estado como rot_sprintf_status() de Hamlib ("MOVING MOVING_AZ ")
    inline std::string statusText(uint32_t st) {
        static const struct { uint32_t f; const char* s; } names[] = {
            { ST_BUSY, "BUSY" }, { ST_MOVING, "MOVING" }, { ST_MOVING_AZ, "MOVING_AZ" }, { ST_MOVING_LEFT, "MOVING_LEFT" },
            { ST_MOVING_RIGHT, "MOVING_RIGHT" }, { ST_MOVING_EL, "MOVING_EL" }, { ST_MOVING_UP, "MOVING_UP" },
            { ST_MOVING_DOWN, "MOVING_DOWN" }, { ST_LIMIT_UP, "LIMIT_UP" }, { ST_LIMIT_DOWN, "LIMIT_DOWN" },
            { ST_LIMIT_LEFT, "LIMIT_LEFT" }, { ST_LIMIT_RIGHT, "LIMIT_RIGHT" }, { ST_OVERLAP_UP, "OVERLAP_UP" },
            { ST_OVERLAP_DOWN, "OVERLAP_DOWN" }, { ST_OVERLAP_LEFT, "OVERLAP_LEFT" }, { ST_OVERLAP_RIGHT, "OVERLAP_RIGHT" },
        };
        std::string s;
        for (auto& n : names) if (st & n.f) { s += n.s; s += ' '; }
        return s;
    }

    // Direccion de "M": numero (ROT_MOVE_*) o nombre
    inline int parseMoveDir(const std::string& t) {
        double v;
        if (number(t, v)) return (int)v;
        std::string u;
        for (char c : t) u += (char)toupper((unsigned char)c);
        if (u == "UP") return MOVE_UP;
        if (u == "DOWN") return MOVE_DOWN;
        if (u == "LEFT" || u == "CCW") return MOVE_LEFT;
        if (u == "RIGHT" || u == "CW") return MOVE_RIGHT;
        return 0;
    }

    class RotctlSession {
    public:
        explicit RotctlSession(int model = 2) : model_(model) {}

        // Procesa bytes del cliente y añade las respuestas a out. Devuelve true si hay que cerrar la conexion ("q")
        bool feed(const char* data, size_t n, Rotor& rot, std::string& out) {
            bool close = false;
            for (size_t i = 0; i < n; i++) {
                const char c = data[i];
                if (c == '\n') {
                    if (handle(line_, rot, out)) close = true;
                    line_.clear();
                } else if (c != '\r') {
                    if (line_.size() < 160) line_ += c;
                    else line_.clear();
                }
            }
            return close;
        }
        void reset() { line_.clear(); }

    private:
        std::string line_;
        int         model_;

        static void rprt(std::string& out, int code) {
            char b[24];
            snprintf(b, sizeof(b), "RPRT %d\n", code == 0 ? 0 : -code);
            out += b;
        }

        bool handle(const std::string& raw0, Rotor& rot, std::string& out) {
            std::string raw = raw0;
            while (!raw.empty() && (raw[0] == ' ' || raw[0] == '\t')) raw.erase(0, 1);
            if (raw.empty()) return false;
            std::string t[6];
            std::string cmd;
            size_t k;
            if (raw[0] == '+' || raw[0] == ';' || raw[0] == '|' || raw[0] == ',') {   // protocolo extendido: no se usa en NET rotctl
                rprt(out, E_NIMPL);
                return false;
            }
            if (raw[0] == '\\') {
                k = split(raw.substr(1), t, 6);
                if (k == 0) return false;
                cmd = t[0];
                for (size_t i = 1; i < k; i++) t[i - 1] = t[i];
                k--;
            } else {
                k = split(raw.substr(1), t, 6);
                switch (raw[0]) {
                    case 'P': cmd = "set_pos"; break;
                    case 'p': cmd = "get_pos"; break;
                    case 'S': cmd = "stop"; break;
                    case 'K': cmd = "park"; break;
                    case 'R': cmd = "reset"; break;
                    case 'M': cmd = "move"; break;
                    case 's': cmd = "get_status"; break;
                    case '_': cmd = "get_info"; break;
                    case '1': cmd = "dump_caps"; break;
                    case 'q': case 'Q': cmd = "quit"; break;
                    default: rprt(out, E_NIMPL); return false;
                }
            }
            char b[160];
            if (cmd == "set_pos") {
                double az, el;
                if (k < 2 || !number(t[0], az) || !number(t[1], el)) { rprt(out, E_INVAL); return false; }
                if (az < -360.0 || az > 720.0 || el < -90.0 || el > 180.0) { rprt(out, E_INVAL); return false; }
                // Acimut dentro del rango mecanico si cabe (rotores de 450 grados); si no, 0-360 y elige el control
                if (az < rot.minAz() || az > rot.maxAz()) az = wrap360(az);
                if (el < rot.minEl()) el = rot.minEl();
                if (el > rot.maxEl()) el = rot.maxEl();
                rprt(out, rot.setPos(az, el) ? E_OK : E_REJECTED);
                return false;
            }
            if (cmd == "get_pos") {
                double az = 0, el = 0;
                rot.getPos(az, el);
                snprintf(b, sizeof(b), "%.6f\n%.6f\n", az, el);
                out += b;
                return false;
            }
            if (cmd == "stop")  { rprt(out, rot.stop() ? E_OK : E_REJECTED); return false; }
            if (cmd == "reset") { rprt(out, rot.stop() ? E_OK : E_REJECTED); return false; }
            if (cmd == "park")  { rprt(out, rot.park() ? E_OK : E_REJECTED); return false; }
            if (cmd == "move") {
                if (k < 1) { rprt(out, E_INVAL); return false; }
                const int dir = parseMoveDir(t[0]);
                double sp = 0;
                if (k >= 2 && !number(t[1], sp)) { rprt(out, E_INVAL); return false; }
                if (dir != MOVE_UP && dir != MOVE_DOWN && dir != MOVE_LEFT && dir != MOVE_RIGHT) { rprt(out, E_INVAL); return false; }
                int speed = (sp < 0) ? 0 : (int)sp;               // -1 = ROT_SPEED_NOCHANGE
                if (speed > 100) speed = 100;
                rprt(out, rot.move(dir, speed) ? E_OK : E_REJECTED);
                return false;
            }
            if (cmd == "get_status") {
                out += statusText(rot.status());
                out += '\n';
                return false;
            }
            if (cmd == "get_info") { snprintf(b, sizeof(b), "%s\n", rot.info()); out += b; return false; }
            if (cmd == "dump_state") {
                // Protocolo 1 (Hamlib 4.x): version, modelo y etiquetas hasta "done". netrotctl lee hasta "done".
                snprintf(b, sizeof(b), "1\n%d\nmin_az=%.6f\nmax_az=%.6f\nmin_el=%.6f\nmax_el=%.6f\nsouth_zero=0\nrot_type=%s\ndone\n",
                         model_, rot.minAz(), rot.maxAz(), rot.minEl(), rot.maxEl(), rot.hasEl() ? "AzEl" : "Az");
                out += b;
                return false;
            }
            if (cmd == "dump_caps") {
                snprintf(b, sizeof(b), "Model name:\tAPRS LoRa+ Rotor\nMfg name:\tEA7JRS\nMin Azimuth:\t%.2f\nMax Azimuth:\t%.2f\n"
                         "Min Elevation:\t%.2f\nMax Elevation:\t%.2f\n", rot.minAz(), rot.maxAz(), rot.minEl(), rot.maxEl());
                out += b;
                return false;
            }
            if (cmd == "chk_vfo") { out += "0\n"; return false; }   // algunos clientes lo prueban al conectar
            if (cmd == "quit") { rprt(out, E_OK); return true; }
            rprt(out, E_NIMPL);
            return false;
        }
    };

}
