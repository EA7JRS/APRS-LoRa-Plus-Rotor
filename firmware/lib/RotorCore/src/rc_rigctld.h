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
// Servidor del protocolo de red de rigctld (Hamlib «NET rigctl» = modelo 2): GPredict, Look4Sat, WSJT-X, fldigi, SDR++... manejan
// la emisora conectada al rotor por WiFi. Órdenes cortas y largas de rigctl (tests/rigctl_parse.c de Hamlib). El \dump_state es el
// de protocolo 0 que usan gqrx y SDR++, que netrigctl de Hamlib 4.x acepta.
#include <stdio.h>
#include <string>
#include "rc_common.h"
#include "rc_cat.h"

namespace RC {

    inline int passband(const std::string& m) {
        if (m == "FM" || m == "PKTFM") return 15000;
        if (m == "WFM") return 230000;
        if (m == "AM") return 6000;
        if (m == "CW" || m == "CWR") return 500;
        if (m == "RTTY" || m == "RTTYR") return 2400;
        if (m.compare(0, 3, "PKT") == 0) return 3000;
        return 2400;
    }

    class RigctlSession {
    public:
        bool feed(const char* data, size_t n, Rig& rig, std::string& out) {
            bool close = false;
            for (size_t i = 0; i < n; i++) {
                const char c = data[i];
                if (c == '\n') { if (handle(line_, rig, out)) close = true; line_.clear(); }
                else if (c != '\r') { if (line_.size() < 200) line_ += c; else line_.clear(); }
            }
            return close;
        }
        void reset() { line_.clear(); }

    private:
        std::string line_;

        static void rprt(std::string& out, int code) {
            char b[24]; snprintf(b, sizeof b, "RPRT %d\n", code == 0 ? 0 : -code); out += b;
        }
        static bool u64(const std::string& s, uint64_t& v) {
            double d;
            if (!number(s, d) || d < 0 || d > 1e11) return false;
            v = (uint64_t)llround(d);
            return true;
        }

        bool handle(const std::string& raw0, Rig& rig, std::string& out) {
            std::string raw = raw0;
            while (!raw.empty() && (raw[0] == ' ' || raw[0] == '\t')) raw.erase(0, 1);
            if (raw.empty()) return false;
            if (raw[0] == '+' || raw[0] == ';' || raw[0] == '|' || raw[0] == ',') { rprt(out, E_NIMPL); return false; }
            std::string t[6], cmd;
            size_t k;
            if (raw[0] == '\\') {
                k = split(raw.substr(1), t, 6);
                if (!k) return false;
                cmd = t[0];
                for (size_t i = 1; i < k; i++) t[i - 1] = t[i];
                k--;
            } else {
                k = split(raw.substr(1), t, 6);
                switch (raw[0]) {
                    case 'f': cmd = "get_freq"; break;        case 'F': cmd = "set_freq"; break;
                    case 'm': cmd = "get_mode"; break;        case 'M': cmd = "set_mode"; break;
                    case 'v': cmd = "get_vfo"; break;         case 'V': cmd = "set_vfo"; break;
                    case 't': cmd = "get_ptt"; break;         case 'T': cmd = "set_ptt"; break;
                    case 's': cmd = "get_split_vfo"; break;   case 'S': cmd = "set_split_vfo"; break;
                    case 'i': cmd = "get_split_freq"; break;  case 'I': cmd = "set_split_freq"; break;
                    case 'x': cmd = "get_split_mode"; break;  case 'X': cmd = "set_split_mode"; break;
                    case 'l': cmd = "get_level"; break;       case 'L': cmd = "set_level"; break;
                    case '_': cmd = "get_info"; break;        case '1': cmd = "dump_caps"; break;
                    case 'q': case 'Q': cmd = "quit"; break;
                    default: rprt(out, E_NIMPL); return false;
                }
            }
            const CatState& st = rig.state();
            char b[96];
            if (cmd == "get_freq") { snprintf(b, sizeof b, "%llu\n", (unsigned long long)st.freq); out += b; return false; }
            if (cmd == "set_freq") {
                uint64_t f;
                if (k < 1 || !u64(t[k - 1], f) || f == 0) { rprt(out, E_INVAL); return false; }
                rprt(out, rig.setFreq(f) ? E_OK : E_REJECTED); return false;
            }
            if (cmd == "get_mode") { snprintf(b, sizeof b, "%s\n%d\n", st.mode.empty() ? "USB" : st.mode.c_str(), passband(st.mode)); out += b; return false; }
            if (cmd == "set_mode") {
                if (k < 1) { rprt(out, E_INVAL); return false; }
                if (t[0] == "?") { out += "LSB USB CW CWR AM FM WFM RTTY RTTYR PKTLSB PKTUSB PKTFM\n"; return false; }
                rprt(out, rig.setMode(t[0]) ? E_OK : E_INVAL); return false;
            }
            if (cmd == "get_vfo") { out += st.vfo == 'B' ? "VFOB\n" : "VFOA\n"; return false; }
            if (cmd == "set_vfo") {
                if (k < 1) { rprt(out, E_INVAL); return false; }
                const std::string v = t[0];
                if (v == "?") { out += "VFOA VFOB Main Sub currVFO\n"; return false; }
                char want = st.vfo;
                if (v == "VFOA" || v == "Main" || v == "MAIN") want = 'A';
                else if (v == "VFOB" || v == "Sub" || v == "SUB") want = 'B';
                else if (v != "currVFO" && v != "VFO") { rprt(out, E_INVAL); return false; }
                rprt(out, rig.setVfo(want) ? E_OK : E_REJECTED); return false;
            }
            if (cmd == "get_ptt") { out += st.ptt ? "1\n" : "0\n"; return false; }
            if (cmd == "set_ptt") {
                double v;
                if (k < 1 || !number(t[k - 1], v)) { rprt(out, E_INVAL); return false; }
                rprt(out, rig.setPtt(v != 0) ? E_OK : E_REJECTED); return false;
            }
            if (cmd == "get_split_vfo") { out += st.split ? "1\n" : "0\n"; out += st.vfo == 'B' ? "VFOA\n" : "VFOB\n"; return false; }
            if (cmd == "set_split_vfo") {
                double v;
                if (k < 1 || !number(t[0], v)) { rprt(out, E_INVAL); return false; }
                rprt(out, rig.setSplit(v != 0) ? E_OK : E_REJECTED); return false;
            }
            if (cmd == "get_split_freq") { snprintf(b, sizeof b, "%llu\n", (unsigned long long)st.txFreq); out += b; return false; }
            if (cmd == "set_split_freq") {
                uint64_t f;
                if (k < 1 || !u64(t[k - 1], f) || f == 0) { rprt(out, E_INVAL); return false; }
                rprt(out, rig.setTxFreq(f) ? E_OK : E_REJECTED); return false;
            }
            if (cmd == "get_split_mode") {
                const std::string& m = st.txMode.empty() ? st.mode : st.txMode;
                snprintf(b, sizeof b, "%s\n%d\n", m.empty() ? "USB" : m.c_str(), passband(m)); out += b; return false;
            }
            if (cmd == "set_split_mode") { rprt(out, E_OK); return false; }          // el modo de subida lo pone la emisora
            if (cmd == "get_level") {
                if (k >= 1 && (t[k - 1] == "STRENGTH" || t[k - 1] == "RAWSTR")) {
                    snprintf(b, sizeof b, "%d\n", t[k - 1] == "RAWSTR" ? (st.smeter < 0 ? 0 : st.smeter * 16) : smeterDb(st.smeter));
                    out += b; return false;
                }
                if (k >= 1 && t[0] == "?") { out += "STRENGTH RAWSTR\n"; return false; }
                rprt(out, E_NIMPL); return false;
            }
            if (cmd == "get_info") { snprintf(b, sizeof b, "%s\n", rig.info()); out += b; return false; }
            if (cmd == "get_powerstat") { out += "1\n"; return false; }
            if (cmd == "chk_vfo") { out += "0\n"; return false; }
            if (cmd == "dump_state") { dumpState(out); return false; }
            if (cmd == "dump_caps") {
                out += "Model name:\tAPRS LoRa+ Rotor CAT\nMfg name:\tEA7JRS\nCan set Frequency:\tY\nCan get Frequency:\tY\n"
                       "Can set Mode:\tY\nCan get Mode:\tY\nCan set PTT:\tY\nCan get PTT:\tY\nCan set Split Freq:\tY\n";
                return false;
            }
            if (cmd == "quit") { rprt(out, E_OK); return true; }
            rprt(out, E_NIMPL);
            return false;
        }

        // Modos de Hamlib: AM 1, CW 2, USB 4, LSB 8, RTTY 0x10, FM 0x20, WFM 0x40, CWR 0x80, RTTYR 0x100, PKTLSB 0x400, PKTUSB 0x800, PKTFM 0x1000
        static void dumpState(std::string& out) {
            out += "0\n"                                            // protocolo
                   "2\n"                                            // modelo (NET rigctl)
                   "2\n"                                            // región ITU
                   "100000.000000 3000000000.000000 0x1dff -1 -1 0x3 0x0\n"     // recepción
                   "0 0 0 0 0 0 0\n"
                   "1800000.000000 3000000000.000000 0x1dbf 1000 100000 0x3 0x0\n"   // emisión
                   "0 0 0 0 0 0 0\n"
                   "0x1dff 1\n0x1dff 10\n0x1dff 100\n0x1dff 1000\n"                  // pasos de sintonía
                   "0 0\n"
                   "0x82 500\n0x82 200\n0x82 2000\n"               // filtros: CW
                   "0x0c 2400\n0x0c 1800\n0x0c 3000\n"             // SSB
                   "0xc00 3000\n"                                   // datos
                   "0x1021 15000\n0x1021 6000\n0x1021 10000\n"     // FM / AM
                   "0x40 230000\n"                                  // WFM
                   "0 0\n"
                   "0\n0\n0\n"                                      // max_rit, max_xit, max_ifshift
                   "0\n"                                            // anuncios
                   "0\n0\n"                                         // previo, atenuador
                   "0\n0\n"                                         // funciones get / set
                   "0x40000000\n"                                   // niveles get: STRENGTH
                   "0\n"                                            // niveles set
                   "0\n0\n";                                        // parámetros get / set
        }
    };

}
