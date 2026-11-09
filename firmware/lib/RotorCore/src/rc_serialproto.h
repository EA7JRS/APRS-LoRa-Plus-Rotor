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
// Protocolos serie de controladores de rotor que Hamlib conoce, en los dos sentidos:
//   - Servidor: la placa se hace pasar por ese controlador (USB, Bluetooth SPP) -> cualquier programa con Hamlib la maneja
//     (modelos 601 GS-232A, 603 GS-232B, 202 EasyComm II, 901 SPID Rot2Prog).
//   - Cliente: la placa manda a un controlador comercial por su puerto serie (pasarela).
// Formatos tomados de rotators/gs232a/gs232a.c y gs232b.c, rotators/spid/spid.c y rotators/easycomm/easycomm.c de Hamlib.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "rc_common.h"

namespace RC {

    enum SerialProto : uint8_t { SP_GS232A = 0, SP_GS232B = 1, SP_EASYCOMM2 = 2, SP_ROT2PROG = 3, SP_RSBUS = 4 };

    inline const char* serialProtoName(int p) {
        switch (p) {
            case SP_GS232A:    return "GS-232A";
            case SP_GS232B:    return "GS-232B";
            case SP_EASYCOMM2: return "EasyComm II";
            case SP_ROT2PROG:  return "SPID Rot2Prog";
            case SP_RSBUS:     return "RS485 bus";
        }
        return "?";
    }

    inline int iround(double v) { return (int)lround(v); }

    // ---------------------------------------------------------------------------------------------------------------
    // Yaesu GS-232A / GS-232B
    // ---------------------------------------------------------------------------------------------------------------
    namespace GS232 {

        // Respuesta de posicion: B = "AZ=aaa  EL=eee\r\n", A = "+0aaa+0eee\r\n"
        inline std::string posReply(bool b, double az, double el, bool azOnly = false, bool elOnly = false) {
            char s[40];
            int a = iround(az), e = iround(el);
            if (a < 0) a = 0;
            if (a > 999) a = 999;
            if (e < 0) e = 0;
            if (e > 999) e = 999;
            if (b) {
                if (azOnly)      snprintf(s, sizeof(s), "AZ=%03d\r\n", a);
                else if (elOnly) snprintf(s, sizeof(s), "EL=%03d\r\n", e);
                else             snprintf(s, sizeof(s), "AZ=%03d  EL=%03d\r\n", a, e);
            } else {
                if (azOnly)      snprintf(s, sizeof(s), "+0%03d\r\n", a);
                else if (elOnly) snprintf(s, sizeof(s), "+0%03d\r\n", e);
                else             snprintf(s, sizeof(s), "+0%03d+0%03d\r\n", a, e);
            }
            return s;
        }

        // Interpreta "AZ=123  EL=045", "AZ=123", "+0123+0045" o "+0123" (respuesta de un controlador)
        inline bool parsePos(const char* s, double& az, double& el, bool& hasEl) {
            while (*s == ' ' || *s == '\r' || *s == '\n') s++;
            int a = 0, e = 0;
            hasEl = false;
            if (strncmp(s, "AZ=", 3) == 0) {
                if (sscanf(s, "AZ=%d EL=%d", &a, &e) == 2) { az = a; el = e; hasEl = true; return true; }
                if (sscanf(s, "AZ=%d", &a) == 1) { az = a; return true; }
                return false;
            }
            if (strncmp(s, "EL=", 3) == 0) {
                if (sscanf(s, "EL=%d", &e) == 1) { el = e; hasEl = true; az = -1; return true; }
                return false;
            }
            if (s[0] == '+') {
                if (sscanf(s, "+0%d+0%d", &a, &e) == 2) { az = a; el = e; hasEl = true; return true; }
                if (sscanf(s, "+%d", &a) == 1) { az = a; return true; }
            }
            return false;
        }

        // Servidor: la placa responde como un GS-232 (B por defecto; A si se elige)
        class Server {
        public:
            explicit Server(bool modeB = true) : b_(modeB) {}
            void setModeB(bool b) { b_ = b; }

            void feed(const char* data, size_t n, Rotor& rot, std::string& out) {
                for (size_t i = 0; i < n; i++) {
                    const char c = data[i];
                    if (c == '\r' || c == '\n') {
                        if (!line_.empty()) handle(line_, rot, out);
                        line_.clear();
                    } else if (line_.size() < 32) {
                        line_ += (char)toupper((unsigned char)c);
                    } else {
                        line_.clear();
                    }
                }
            }
            int speed() const { return speed_; }

        private:
            bool        b_;
            int         speed_ = 4;       // X1..X4 -> 25..100 %
            std::string line_;

            void bad(std::string& out) { out += "?>\r\n"; }

            void handle(const std::string& l, Rotor& rot, std::string& out) {
                double az, el;
                switch (l[0]) {
                    case 'C':
                        rot.getPos(az, el);
                        if (l == "C")       out += posReply(b_, az, el, true);
                        else if (l == "C2") out += posReply(b_, az, el, !rot.hasEl());
                        else bad(out);
                        return;
                    case 'B':
                        if (l != "B") { bad(out); return; }
                        rot.getPos(az, el);
                        out += posReply(b_, az, el, false, true);
                        return;
                    case 'W': {
                        int a = 0, e = 0;
                        if (sscanf(l.c_str() + 1, "%d %d", &a, &e) != 2 || a < 0 || a > 450 || e < 0 || e > 180) { bad(out); return; }
                        if (!rot.setPos(a, e)) bad(out);
                        return;
                    }
                    case 'M': {
                        int a = 0;
                        if (sscanf(l.c_str() + 1, "%d", &a) != 1 || a < 0 || a > 450) { bad(out); return; }
                        if (!rot.setAz(a)) bad(out);
                        return;
                    }
                    case 'S': if (l == "S") rot.stop(); else bad(out); return;
                    case 'A': if (l == "A") rot.stopAz(); else bad(out); return;
                    case 'E': if (l == "E") rot.stopEl(); else bad(out); return;
                    case 'L': if (l == "L") rot.move(MOVE_LEFT, speed_ * 25); else bad(out); return;
                    case 'R': if (l == "R") rot.move(MOVE_RIGHT, speed_ * 25); else bad(out); return;
                    case 'U': if (l == "U") rot.move(MOVE_UP, speed_ * 25); else bad(out); return;
                    case 'D': if (l == "D") rot.move(MOVE_DOWN, speed_ * 25); else bad(out); return;
                    case 'X': {
                        const int s = (l.size() == 2) ? l[1] - '0' : 0;
                        if (s >= 1 && s <= 4) speed_ = s; else bad(out);
                        return;
                    }
                    case 'P':                       // P36 / P45: modo de 360 / 450 grados (el rango lo fija la configuracion)
                        if (l == "P36" || l == "P45") return;
                        bad(out);
                        return;
                    case 'Z': return;               // centro norte / sur: lo fija la configuracion
                    case 'H':
                        out += "APRS LoRa+ Rotor GS-232: C C2 B Waaa eee Maaa S A E L R U D X1-4\r\n";
                        return;
                    default:
                        bad(out);
                }
            }
        };

        // Cliente (pasarela a un controlador GS-232): ordenes listas para mandar
        inline std::string cmdSet(double az, double el, bool hasEl) {
            char s[24];
            int a = iround(az < 0 ? az + 360 : az), e = iround(el);
            if (e < 0) e = 0;
            if (hasEl) snprintf(s, sizeof(s), "W%03d %03d\r", a, e);
            else       snprintf(s, sizeof(s), "M%03d\r", a);
            return s;
        }
        inline std::string cmdQuery(bool hasEl) { return hasEl ? "C2\r" : "C\r"; }
        inline std::string cmdStop() { return "S\r"; }
        inline std::string cmdMove(int dir) {
            switch (dir) { case MOVE_LEFT: return "L\r"; case MOVE_RIGHT: return "R\r"; case MOVE_UP: return "U\r"; case MOVE_DOWN: return "D\r"; }
            return "";
        }
        inline std::string cmdSpeed(int pct) {
            int x = (pct + 24) / 25;
            if (x < 1) x = 1;
            if (x > 4) x = 4;
            char s[8];
            snprintf(s, sizeof(s), "X%d\r", x);
            return s;
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // EasyComm II
    // ---------------------------------------------------------------------------------------------------------------
    namespace EasyComm {

        class Server {
        public:
            void feed(const char* data, size_t n, Rotor& rot, std::string& out) {
                for (size_t i = 0; i < n; i++) {
                    const char c = data[i];
                    if (c == '\r' || c == '\n') {
                        if (!line_.empty()) handleLine(line_, rot, out);
                        line_.clear();
                    } else if (line_.size() < 96) {
                        line_ += (char)toupper((unsigned char)c);
                    } else {
                        line_.clear();
                    }
                }
            }

        private:
            std::string line_;

            void handleLine(const std::string& l, Rotor& rot, std::string& out) {
                std::string t[12];
                const size_t k = split(l, t, 12);
                double az, el, nAz = NAN, nEl = NAN;
                bool askAz = false, askEl = false;
                for (size_t i = 0; i < k; i++) {
                    const std::string& w = t[i];
                    if (w.size() >= 2 && w[0] == 'A' && w[1] == 'Z') {
                        double v;
                        if (w.size() == 2) askAz = true;
                        else if (number(w.substr(2), v)) nAz = v;
                    } else if (w.size() >= 2 && w[0] == 'E' && w[1] == 'L') {
                        double v;
                        if (w.size() == 2) askEl = true;
                        else if (number(w.substr(2), v)) nEl = v;
                    } else if (w == "SA") rot.stopAz();
                    else if (w == "SE") rot.stopEl();
                    else if (w == "ML") rot.move(MOVE_LEFT, 0);
                    else if (w == "MR") rot.move(MOVE_RIGHT, 0);
                    else if (w == "MU") rot.move(MOVE_UP, 0);
                    else if (w == "MD") rot.move(MOVE_DOWN, 0);
                    else if (w == "PARK") rot.park();
                    else if (w == "RESET") rot.stop();
                    else if (w == "VE") out += "VEAPRS LoRa+ Rotor\n";
                    else if (w == "GS") {
                        char b[16];
                        const uint32_t s = rot.status();
                        snprintf(b, sizeof(b), "GS%d\n", (s & ST_MOVING) ? 2 : 1);   // 1 = parado, 2 = moviendose
                        out += b;
                    }
                    // UP/DN/frecuencias y el resto: se ignoran
                }
                if (!isnan(nAz) || !isnan(nEl)) {
                    rot.getPos(az, el);
                    const double ta = isnan(nAz) ? az : nAz;
                    const double te = isnan(nEl) ? el : nEl;
                    if (!isnan(nEl)) rot.setPos(ta, te);
                    else rot.setAz(ta);
                }
                if (askAz || askEl) {
                    rot.getPos(az, el);
                    char b[40];
                    if (askAz && askEl) snprintf(b, sizeof(b), "AZ%.1f EL%.1f\n", az, el);
                    else if (askAz)     snprintf(b, sizeof(b), "AZ%.1f\n", az);
                    else                snprintf(b, sizeof(b), "EL%.1f\n", el);
                    out += b;
                }
            }
        };

        inline std::string cmdSet(double az, double el) {
            char s[40];
            snprintf(s, sizeof(s), "AZ%.1f EL%.1f\n", wrap360(az), el < 0 ? 0.0 : el);
            return s;
        }
        inline std::string cmdQueryAz() { return "AZ\n"; }
        inline std::string cmdQueryEl() { return "EL\n"; }
        inline std::string cmdStop() { return "SA SE \n"; }
        inline std::string cmdMove(int dir) {
            switch (dir) { case MOVE_LEFT: return "ML\n"; case MOVE_RIGHT: return "MR\n"; case MOVE_UP: return "MU\n"; case MOVE_DOWN: return "MD\n"; }
            return "";
        }
        // "AZ123.4", "EL45.0" o las dos en la misma linea
        inline bool parseReply(const char* s, double& az, double& el, bool& gotAz, bool& gotEl) {
            gotAz = gotEl = false;
            const char* p = strstr(s, "AZ");
            if (p) { float v; if (sscanf(p, "AZ%f", &v) == 1) { az = v; gotAz = true; } }
            p = strstr(s, "EL");
            if (p) { float v; if (sscanf(p, "EL%f", &v) == 1) { el = v; gotEl = true; } }
            return gotAz || gotEl;
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // SPID Rot2Prog (tramas binarias de 13 bytes; respuesta de 12)
    // ---------------------------------------------------------------------------------------------------------------
    namespace Rot2Prog {

        enum : uint8_t { K_STOP = 0x0F, K_STATUS = 0x1F, K_SET = 0x2F };

        // Orden de colocar: u = PH * (360 + az) en 4 cifras ASCII
        inline void frameSet(uint8_t f[13], double az, double el, uint8_t ph = 10, uint8_t pv = 10) {
            const long ua = lround(ph * (360.0 + az)), ue = lround(pv * (360.0 + el));
            f[0] = 0x57;
            f[1] = (uint8_t)('0' + (ua / 1000) % 10); f[2] = (uint8_t)('0' + (ua / 100) % 10);
            f[3] = (uint8_t)('0' + (ua / 10) % 10);   f[4] = (uint8_t)('0' + ua % 10);
            f[5] = ph;
            f[6] = (uint8_t)('0' + (ue / 1000) % 10); f[7] = (uint8_t)('0' + (ue / 100) % 10);
            f[8] = (uint8_t)('0' + (ue / 10) % 10);   f[9] = (uint8_t)('0' + ue % 10);
            f[10] = pv;
            f[11] = K_SET;
            f[12] = 0x20;
        }
        inline void frameSimple(uint8_t f[13], uint8_t k) {
            memset(f, 0, 13);
            f[0] = 0x57;
            f[11] = k;
            f[12] = 0x20;
        }
        // Respuesta (12 bytes): 0x57 H1 H2 H3 H4 PH V1 V2 V3 V4 PV 0x20; az = H1*100 + H2*10 + H3 + H4/10 - 360
        inline void reply(uint8_t r[12], double az, double el) {
            long ua = lround((360.0 + az) * 10.0), ue = lround((360.0 + el) * 10.0);
            if (ua < 0) ua = 0;
            if (ue < 0) ue = 0;
            r[0] = 0x57;
            r[1] = (uint8_t)((ua / 1000) % 10); r[2] = (uint8_t)((ua / 100) % 10); r[3] = (uint8_t)((ua / 10) % 10); r[4] = (uint8_t)(ua % 10);
            r[5] = 10;
            r[6] = (uint8_t)((ue / 1000) % 10); r[7] = (uint8_t)((ue / 100) % 10); r[8] = (uint8_t)((ue / 10) % 10); r[9] = (uint8_t)(ue % 10);
            r[10] = 10;
            r[11] = 0x20;
        }
        inline bool parseReply(const uint8_t r[12], double& az, double& el) {
            if (r[0] != 0x57 || r[11] != 0x20) return false;
            for (int i : {1, 2, 3, 4, 6, 7, 8, 9}) if (r[i] > 9) return false;
            az = r[1] * 100 + r[2] * 10 + r[3] + r[4] / 10.0 - 360.0;
            el = r[6] * 100 + r[7] * 10 + r[8] + r[9] / 10.0 - 360.0;
            return true;
        }

        // Servidor: la placa se comporta como un Rot2Prog / MD-01 (responde tambien a la orden de colocar, como el MD-01)
        class Server {
        public:
            void feed(const uint8_t* data, size_t n, Rotor& rot, std::string& out) {
                for (size_t i = 0; i < n; i++) {
                    const uint8_t c = data[i];
                    if (len_ == 0 && c != 0x57) continue;           // resincroniza en el byte de inicio
                    buf_[len_++] = c;
                    if (len_ == 13) {
                        len_ = 0;
                        if (buf_[12] != 0x20) continue;
                        handle(rot, out);
                    }
                }
            }
        private:
            uint8_t buf_[13];
            size_t  len_ = 0;

            static bool digits(const uint8_t* p, long& v) {
                v = 0;
                for (int i = 0; i < 4; i++) { if (p[i] < '0' || p[i] > '9') return false; v = v * 10 + (p[i] - '0'); }
                return true;
            }
            void handle(Rotor& rot, std::string& out) {
                const uint8_t k = buf_[11];
                if (k == K_STOP) rot.stop();
                else if (k == K_SET) {
                    long ua, ue;
                    const uint8_t ph = buf_[5] ? buf_[5] : 10, pv = buf_[10] ? buf_[10] : 10;
                    if (digits(buf_ + 1, ua) && digits(buf_ + 6, ue)) {
                        double az = (double)ua / ph - 360.0, el = (double)ue / pv - 360.0;
                        if (az < 0) az += 360.0;
                        if (el < 0) el = 0;
                        rot.setPos(az, el);
                    }
                } else if (k != K_STATUS) {
                    return;                                       // otras ordenes (mover con 0x14...): sin respuesta
                }
                double az, el;
                rot.getPos(az, el);
                uint8_t r[12];
                reply(r, az, el);
                out.append((const char*)r, 12);
            }
        };
    }


    // ---------------------------------------------------------------------------------------------------------------
    // Bus RS485 con direcciones: tramas de texto "#ORIGEN:DESTINO:ORDEN:DATOS:SUMA$". Un maestro (la placa) y un esclavo por
    // eje (acimut y elevación con direcciones distintas). SUMA = ORIGEN + DESTINO + el último número de DATOS (0 si no hay).
    // Los decimales van con coma ("160,50"); se aceptan también con punto. Respuestas "ACK_<ORDEN>" o "NAK_<ORDEN>"; los
    // fallos llegan solos como "ERR" a la dirección 255.
    // ---------------------------------------------------------------------------------------------------------------
    namespace RsBus {

        static const int BROADCAST = 255;

        // Último número ([-+]dígitos[(,|.)dígitos]) del texto; 0 si no hay ninguno
        inline double lastNumber(const std::string& p) {
            double last = 0;
            size_t i = 0;
            while (i < p.size()) {
                size_t j = i;
                if (p[j] == '+' || p[j] == '-') j++;
                if (j < p.size() && isdigit((unsigned char)p[j])) {
                    size_t k = j;
                    while (k < p.size() && isdigit((unsigned char)p[k])) k++;
                    if (k + 1 < p.size() && (p[k] == ',' || p[k] == '.') && isdigit((unsigned char)p[k + 1])) {
                        k++;
                        while (k < p.size() && isdigit((unsigned char)p[k])) k++;
                    }
                    std::string t = p.substr(i, k - i);
                    for (char& c : t) if (c == ',') c = '.';
                    last = strtod(t.c_str(), nullptr);
                    i = k;
                } else {
                    i++;
                }
            }
            return last;
        }

        // Número con coma y como mucho 2 decimales, sin ceros de sobra ("187", "160,5")
        inline std::string fmtNum(double v) {
            char b[32];
            const double r = round(v);
            if (fabs(v - r) < 0.005) { snprintf(b, sizeof b, "%ld", (long)r); return b; }
            snprintf(b, sizeof b, "%.2f", v);
            std::string s = b;
            while (!s.empty() && s.back() == '0') s.pop_back();
            for (char& c : s) if (c == '.') c = ',';
            return s;
        }

        // Grados con dos decimales ("160,00")
        inline std::string fmtDeg(double v) {
            char b[24];
            snprintf(b, sizeof b, "%.2f", v);
            for (char* c = b; *c; c++) if (*c == '.') *c = ',';
            return b;
        }

        inline std::string frame(int src, int dst, const char* cmd, const std::string& params) {
            return "#" + std::to_string(src) + ":" + std::to_string(dst) + ":" + cmd + ":" + params + ":" +
                   fmtNum(src + dst + lastNumber(params)) + "$";
        }

        struct Msg { int src = 0, dst = 0; std::string cmd, params; };

        // Interpreta lo que hay entre '#' y '$' (sin ellos) y comprueba la suma
        inline bool parse(const std::string& body, Msg& m) {
            const size_t a = body.find(':');
            if (a == std::string::npos) return false;
            const size_t b = body.find(':', a + 1);
            if (b == std::string::npos) return false;
            const size_t c = body.find(':', b + 1);
            const size_t z = body.rfind(':');
            if (c == std::string::npos || z <= c) return false;
            char* e;
            m.src = (int)strtol(body.c_str(), &e, 10);
            if (e != body.c_str() + a) return false;
            m.dst = (int)strtol(body.c_str() + a + 1, &e, 10);
            if (e != body.c_str() + b) return false;
            m.cmd = body.substr(b + 1, c - b - 1);
            m.params = body.substr(c + 1, z - c - 1);
            std::string cs = body.substr(z + 1);
            for (char& ch : cs) if (ch == ',') ch = '.';
            const double sum = strtod(cs.c_str(), &e);
            if (cs.empty() || *e) return false;
            return fabs(sum - (m.src + m.dst + lastNumber(m.params))) < 0.011;
        }

        // Recoge tramas de un flujo de bytes
        class Reader {
        public:
            // true cuando hay una trama completa y válida en m
            bool push(char c, Msg& m) {
                if (c == '#') { buf_.clear(); in_ = true; return false; }
                if (!in_) return false;
                if (c == '$') { in_ = false; return parse(buf_, m); }
                if (buf_.size() >= 160) { in_ = false; return false; }
                buf_ += c;
                return false;
            }
        private:
            std::string buf_;
            bool        in_ = false;
        };

        inline double num(const std::string& s) {
            std::string t = s.substr(0, s.find(';'));
            for (char& c : t) if (c == ',') c = '.';
            return strtod(t.c_str(), nullptr);
        }
    }

}
