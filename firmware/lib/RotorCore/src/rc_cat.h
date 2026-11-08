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
// CAT de emisoras (C++ puro, sin Arduino). Protocolos:
//   CAT_CIV      Icom CI-V (IC-705, IC-7300, IC-7610, IC-9700, IC-910...): FE FE dir ctl orden ... FD, frecuencia en BCD.
//   CAT_YAESU    Yaesu de texto (FT-991/991A, FT-891, FT-710, FTDX10, FTDX101): FA...; MD0x; TX; SM0;
//   CAT_YAESU5   Yaesu de 5 bytes (FT-817/818, FT-857, FT-897): 4 parámetros + código, respuestas de longitud fija.
//   CAT_KENWOOD  Kenwood / Elecraft (TS-2000, TS-480, TS-590, TS-890, K3, KX3...): FA...; IF; MD; TX; RX; SM0;
//   CAT_SDRCONNECT  SDRconnect de SDRplay (RSPdx-R2, RSP1B, RSPduo...) por su API WebSocket (puerto 5454): receptor, sin PTT.
//   CAT_RIGCTL   cliente de rigctld por red: SDR++ (4532), gqrx (7356), rigctld de Hamlib...
// Cada motor guarda lo que sabe de la emisora (CatState), pide frecuencia, modo, PTT y S-meter por turnos y pone en cola las
// órdenes. Formatos tomados de los manuales CAT de Icom, Yaesu y Kenwood y de los controladores de Hamlib (icom.c, newcat.c,
// ft817.c, kenwood.c). Nombres de modo: los de Hamlib (LSB, USB, CW, CWR, AM, FM, WFM, RTTY, RTTYR, PKTLSB, PKTUSB, PKTFM).
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <deque>
#include <math.h>
#include "rc_common.h"

namespace RC {

    enum CatProto : int { CAT_NONE = 0, CAT_CIV = 1, CAT_YAESU = 2, CAT_YAESU5 = 3, CAT_KENWOOD = 4, CAT_SDRCONNECT = 5, CAT_RIGCTL = 6 };
    inline bool catIsNetwork(int p) { return p == CAT_SDRCONNECT || p == CAT_RIGCTL; }

    inline const char* catProtoName(int p) {
        switch (p) {
            case CAT_CIV: return "CI-V";
            case CAT_YAESU: return "Yaesu";
            case CAT_YAESU5: return "Yaesu 5 bytes";
            case CAT_KENWOOD: return "Kenwood";
            case CAT_SDRCONNECT: return "SDRconnect";
            case CAT_RIGCTL: return "rigctl";
        }
        return "-";
    }

    struct CatState {
        bool        link = false;           // la emisora contesta
        uint64_t    freq = 0;               // Hz (VFO de recepción)
        uint64_t    txFreq = 0;             // Hz del otro VFO / banda (split, satélite); 0 = no se sabe
        std::string mode;                   // nombres de Hamlib
        std::string txMode;
        char        vfo = 'A';
        bool        ptt = false;
        bool        split = false;
        int         smeter = -1;            // 0-15: S0..S9 = 0..9, S9+10..+60 = 10..15; -1 sin dato
        int         tone = 0;               // CTCSS de emisión en décimas de Hz (670 = 67,0 Hz); 0 = apagado
    };

    // Tonos CTCSS en décimas de Hz: la lista de 50 de Yaesu (orden CN) y la de 42 de Kenwood (orden TN)
    static const int CTCSS50[50] = {670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
        1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679, 1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
        2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541};
    static const int CTCSS42[42] = {670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
        1318, 1365, 1413, 1462, 1514, 1567, 1622, 1679, 1738, 1799, 1862, 1928, 2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 17500};
    // Posición del tono en la tabla (admite ±0,2 Hz) o -1
    inline int toneIndex(const int* t, int n, int dHz) { for (int i = 0; i < n; i++) if (dHz >= t[i] - 2 && dHz <= t[i] + 2) return i; return -1; }

    // dB sobre S9 como el nivel STRENGTH de Hamlib (S9 = 0, S0 = -54)
    inline int smeterDb(int s) { return s < 0 ? -54 : (s <= 9 ? (s - 9) * 6 : (s - 9) * 10); }
    inline int smeterFrom255(int v) { if (v < 0) return -1; if (v <= 120) return (v * 9 + 60) / 120; const int s = 9 + ((v - 120) * 6 + 60) / 121; return s > 15 ? 15 : s; }
    inline int smeterFrom30(int v)  { if (v < 0) return -1; if (v <= 15) return (v * 9 + 7) / 15; const int s = 9 + ((v - 15) * 6 + 7) / 15; return s > 15 ? 15 : s; }

    // Normaliza un nombre de modo (de la app, la pantalla o rigctld): "DIG"/"DATA" -> PKTUSB, "FM-N" -> FM...
    inline std::string normMode(const std::string& in) {
        std::string m;
        for (char c : in) if (c != '-' && c != '_' && c != ' ') m += (char)toupper((unsigned char)c);
        if (m == "DIG" || m == "DATA" || m == "DATAU" || m == "USBD" || m == "PKT" || m == "DIGU") return "PKTUSB";
        if (m == "DATAL" || m == "LSBD" || m == "DIGL") return "PKTLSB";
        if (m == "DATAFM" || m == "FMD") return "PKTFM";
        if (m == "FMN" || m == "NFM") return "FM";
        if (m == "AMN") return "AM";
        if (m == "CWU") return "CW";
        if (m == "CWL" || m == "CWREV") return "CWR";
        if (m == "FSK") return "RTTY";
        if (m == "FSKR" || m == "RTTYREV") return "RTTYR";
        return m;
    }

    // Doppler: rr = velocidad radial en km/s (+ = se aleja). Bajada que se recibe y subida que hay que emitir.
    inline uint64_t dopplerRx(uint64_t f, double rrKmS) { return (uint64_t)llround((double)f * (1.0 - rrKmS / 299792.458)); }
    inline uint64_t dopplerTx(uint64_t f, double rrKmS) { return (uint64_t)llround((double)f * (1.0 + rrKmS / 299792.458)); }

    // Lo que ofrece una emisora a rigctld, la API JSON, la pantalla y el seguimiento
    class Rig {
    public:
        virtual ~Rig() {}
        virtual const CatState& state() const = 0;
        virtual bool setFreq(uint64_t hz) = 0;
        virtual bool setMode(const std::string& mode) = 0;
        virtual bool setPtt(bool on) = 0;                 // false: no permitido o no se puede
        virtual bool toggleVfo() = 0;
        virtual bool setVfo(char v) { return v == state().vfo ? true : toggleVfo(); }
        virtual bool setTxFreq(uint64_t hz) = 0;          // otro VFO / banda (split, satélite); false si la emisora no puede
        virtual bool setSplit(bool on) = 0;
        // CTCSS de emisión (subida de los satélites FM: SO-50, PO-101...): décimas de Hz, 0 = apagado. false si la emisora no puede
        virtual bool setTone(int dHz) { (void)dHz; return false; }
        virtual const char* info() const = 0;
    };

    // ---------------------------------------------------------------------------------------------------------------- base
    class CatEngine : public Rig {
    public:
        bool     allowPtt = false;
        uint32_t pollGapMs = 120;          // pausa entre consultas
        uint32_t replyMs = 400;            // espera de respuesta
        uint32_t cmdGapMs = 30;            // pausa mínima entre órdenes seguidas
        std::string name;                  // "IC-9700 · CI-V A2 · 19200"

        const CatState& state() const override { return st_; }
        const char* info() const override { return name.c_str(); }

        // Bytes que hay que mandar ahora (órdenes en cola o la siguiente consulta). Llamar en cada vuelta del bucle.
        void tick(uint32_t now, std::string& out) {
            if (waiting_) {
                if (now - sentAt_ < replyMs) return;
                waiting_ = false;                               // sin respuesta
                expectLen_ = 0;
                if (++misses_ >= 4 && st_.link) { st_.link = false; st_.smeter = -1; }
            }
            if (sent_ && !q_.empty() && now - sentAt_ < (q_.front().urgent ? cmdGapMs : pollGapMs)) return;
            if (q_.empty()) {
                if (sent_ && now - sentAt_ < pollGapMs) return;
                Item it;
                it.reply = true;
                if (!poll(pollIdx_++, it)) { pollIdx_ = 0; if (!poll(pollIdx_++, it)) return; }
                q_.push_back(it);
            }
            Item it = q_.front();
            q_.pop_front();
            out += it.data;
            sentAt_ = now;
            sent_ = true;
            waiting_ = it.reply;
            expectLen_ = it.len;
            expectKind_ = it.kind;
            if (it.reply) rx_.clear();
        }

        // Bytes que llegan de la emisora
        void feed(const uint8_t* d, size_t n, uint32_t now) {
            for (size_t i = 0; i < n; i++) {
                rx_ += (char)d[i];
                if (rx_.size() > 256) rx_.erase(0, rx_.size() - 64);
                parse(now);
            }
        }

        bool setPtt(bool on) override {
            if (on && (!allowPtt || receiver())) return false;
            return cmdPtt(on);
        }

        size_t queued() const { return q_.size(); }
        virtual bool receiver() const { return false; }         // solo recibe (SDR): sin PTT ni subida
        // Enlaces por red: el transporte avisa al conectar y al cortarse
        virtual void onConnect(uint32_t now) { (void)now; reset(); }
        virtual void onDisconnect() { reset(); st_.link = false; st_.smeter = -1; }
        void reset() { q_.clear(); rx_.clear(); waiting_ = false; sent_ = false; expectLen_ = 0; pollIdx_ = 0; misses_ = 0; }

    protected:
        struct Item {
            std::string data;
            bool        reply = false;      // espera respuesta
            bool        urgent = false;     // órdenes: van sin la pausa de las consultas
            int         len = 0;            // longitud fija de la respuesta (Yaesu 5 bytes)
            int         kind = 0;
        };
        CatState          st_;
        std::deque<Item>  q_;
        std::string       rx_;
        bool              waiting_ = false, sent_ = false;
        uint32_t          sentAt_ = 0;
        int               misses_ = 0, pollIdx_ = 0, expectLen_ = 0, expectKind_ = 0;

        void queue(const std::string& d, bool reply = false, int len = 0, int kind = 0) {
            if (q_.size() > 16) q_.pop_front();
            Item it; it.data = d; it.reply = reply; it.urgent = true; it.len = len; it.kind = kind;
            q_.push_back(it);
        }
        void gotReply() { waiting_ = false; misses_ = 0; st_.link = true; }

        virtual bool poll(int idx, Item& it) = 0;        // false: ya no hay más consultas en la ronda
        virtual void parse(uint32_t now) = 0;
        virtual bool cmdPtt(bool on) = 0;
    };

    // ---------------------------------------------------------------------------------------------------------------- Icom CI-V
    class CivEngine : public CatEngine {
    public:
        uint8_t addr = 0xA2, ctl = 0xE0;

        bool setFreq(uint64_t hz) override { queue(frame(0x05, -1, bcd(hz, 5)), true); st_.freq = hz; return true; }
        bool setTxFreq(uint64_t hz) override { queue(frame(0x25, 0x01, bcd(hz, 5)), true); st_.txFreq = hz; return true; }
        bool setSplit(bool on) override { queue(frame(0x0F, on ? 0x01 : 0x00, ""), true); st_.split = on; return true; }
        bool toggleVfo() override { queue(frame(0x07, 0xB0, ""), true); st_.vfo = st_.vfo == 'A' ? 'B' : 'A'; return true; }
        // 1B 00: tono de repetidor, 3 bytes BCD con la cifra más significativa primero (00 08 85 = 88,5 Hz); 16 42: tono de repetidor sí / no
        bool setTone(int dHz) override {
            if (dHz < 0 || dHz > 2600) return false;
            if (dHz > 0) {
                std::string d;
                d += (char)0; d += (char)(((dHz / 1000) % 10) << 4 | ((dHz / 100) % 10)); d += (char)(((dHz / 10) % 10) << 4 | (dHz % 10));
                queue(frame(0x1B, 0x00, d), true);
            }
            queue(frame(0x16, 0x42, std::string(1, dHz > 0 ? '\x01' : '\x00')), true);
            st_.tone = dHz;
            return true;
        }
        bool setMode(const std::string& mode) override {
            const std::string m = normMode(mode);
            int code = -1, data = -1;
            if (m == "LSB") code = 0x00; else if (m == "USB") code = 0x01; else if (m == "AM") code = 0x02;
            else if (m == "CW") code = 0x03; else if (m == "RTTY") code = 0x04; else if (m == "FM") code = 0x05;
            else if (m == "WFM") code = 0x06; else if (m == "CWR") code = 0x07; else if (m == "RTTYR") code = 0x08;
            else if (m == "DV") code = 0x17;
            else if (m == "PKTUSB") { code = 0x01; data = 1; } else if (m == "PKTLSB") { code = 0x00; data = 1; }
            else if (m == "PKTFM") { code = 0x05; data = 1; }
            if (code < 0) return false;
            queue(frame(0x06, -1, std::string(1, (char)code)), true);
            if (code <= 0x05) queue(frame(0x1A, 0x06, data == 1 ? std::string("\x01\x01", 2) : std::string("\x00\x00", 2)), true);
            st_.mode = m;
            return true;
        }

        static std::string bcd(uint64_t v, int bytes) {         // menos significativo primero
            std::string s;
            for (int i = 0; i < bytes; i++) { const int lo = (int)(v % 10); v /= 10; const int hi = (int)(v % 10); v /= 10; s += (char)((hi << 4) | lo); }
            return s;
        }
        static uint64_t unbcd(const uint8_t* p, int n) {
            uint64_t v = 0, m = 1;
            for (int i = 0; i < n; i++) { v += (uint64_t)(p[i] & 0x0F) * m; m *= 10; v += (uint64_t)(p[i] >> 4) * m; m *= 10; }
            return v;
        }
        std::string frame(uint8_t cmd, int sub, const std::string& data) const {
            std::string f = "\xFE\xFE";
            f += (char)addr; f += (char)ctl; f += (char)cmd;
            if (sub >= 0) f += (char)sub;
            f += data;
            f += '\xFD';
            return f;
        }

    protected:
        enum { K_SUB = 5 };
        bool dataMode_ = false, noSub_ = false;

        bool cmdPtt(bool on) override { queue(frame(0x1C, 0x00, std::string(1, on ? '\x01' : '\x00')), true); st_.ptt = on; return true; }

        bool poll(int idx, Item& it) override {
            switch (idx) {
                case 0: it.data = frame(0x03, -1, ""); return true;
                case 1: it.data = frame(0x04, -1, ""); return true;
                case 2: it.data = frame(0x1C, 0x00, ""); return true;
                case 3: it.data = frame(0x15, 0x02, ""); return true;
                case 4: it.data = frame(0x1A, 0x06, ""); return true;
                case 5: if (noSub_) return false; it.data = frame(0x25, 0x01, ""); it.kind = K_SUB; return true;
            }
            return false;
        }

        void parse(uint32_t) override {
            // Busca tramas completas FE FE ... FD
            for (;;) {
                const size_t s = rx_.find("\xFE\xFE");
                if (s == std::string::npos) { if (rx_.size() > 1) rx_.erase(0, rx_.size() - 1); return; }
                if (s) rx_.erase(0, s);
                size_t e = rx_.find('\xFD', 2);
                if (e == std::string::npos) return;
                const std::string f = rx_.substr(0, e + 1);
                rx_.erase(0, e + 1);
                handleFrame((const uint8_t*)f.data(), f.size());
            }
        }

        void handleFrame(const uint8_t* f, size_t n) {
            while (n >= 3 && f[2] == 0xFE) { f++; n--; }                // FE de más
            if (n < 6) return;
            const uint8_t to = f[2], from = f[3], cmd = f[4];
            if (from == ctl) return;                                    // eco de lo que mandamos (bus de un hilo)
            if (from != addr || (to != ctl && to != 0x00)) return;
            const uint8_t* d = f + 5;
            const int dn = (int)n - 6;                                  // datos tras la orden, sin FD
            switch (cmd) {
                case 0xFB: gotReply(); return;
                case 0xFA: if (expectKind_ == K_SUB) noSub_ = true; gotReply(); return;
                case 0x00: case 0x03:
                    if (dn >= 4) { st_.freq = unbcd(d, dn > 5 ? 5 : dn); }
                    if (cmd == 0x03) gotReply();
                    return;
                case 0x01: case 0x04:
                    if (dn >= 1) setModeFromCode(d[0]);
                    if (cmd == 0x04) gotReply();
                    return;
                case 0x1C: if (dn >= 2 && d[0] == 0x00) { st_.ptt = d[1] != 0; gotReply(); } return;
                case 0x15: if (dn >= 3 && d[0] == 0x02) { st_.smeter = smeterFrom255(bcd2int(d + 1)); gotReply(); } return;
                case 0x1A: if (dn >= 2 && d[0] == 0x06) { dataMode_ = d[1] != 0; refreshMode(); gotReply(); } return;
                case 0x25: if (dn >= 5 && d[0] == 0x01) { st_.txFreq = unbcd(d + 1, dn - 1 > 5 ? 5 : dn - 1); gotReply(); } return;
            }
        }

        static int bcd2int(const uint8_t* p) {                          // S-meter: 2 bytes BCD, el más significativo primero (0000-0255)
            return ((p[0] >> 4) * 1000) + ((p[0] & 15) * 100) + ((p[1] >> 4) * 10) + (p[1] & 15);
        }

        uint8_t modeCode_ = 0xFF;
        void setModeFromCode(uint8_t c) { modeCode_ = c; refreshMode(); }
        void refreshMode() {
            const char* m = "";
            switch (modeCode_) {
                case 0x00: m = dataMode_ ? "PKTLSB" : "LSB"; break;
                case 0x01: m = dataMode_ ? "PKTUSB" : "USB"; break;
                case 0x02: m = "AM"; break;
                case 0x03: m = "CW"; break;
                case 0x04: m = "RTTY"; break;
                case 0x05: m = dataMode_ ? "PKTFM" : "FM"; break;
                case 0x06: m = "WFM"; break;
                case 0x07: m = "CWR"; break;
                case 0x08: m = "RTTYR"; break;
                case 0x17: m = "DV"; break;
                default: return;
            }
            st_.mode = m;
        }
    };

    // ---------------------------------------------------------------------------------------------------------------- texto (Yaesu / Kenwood)
    class AsciiCat : public CatEngine {
    protected:
        void parse(uint32_t) override {
            for (;;) {
                const size_t e = rx_.find(';');
                if (e == std::string::npos) return;
                std::string r = rx_.substr(0, e);
                rx_.erase(0, e + 1);
                while (!r.empty() && (r[0] == '\r' || r[0] == '\n' || r[0] == ' ')) r.erase(0, 1);
                if (r == "?" || r == "E" || r == "O") { gotReply(); continue; }   // orden no admitida
                if (r.size() >= 2) answer(r);
            }
        }
        virtual void answer(const std::string& r) = 0;
        static bool digits(const std::string& s, size_t from, size_t n, uint64_t& v) {
            if (s.size() < from + n) return false;
            v = 0;
            for (size_t i = from; i < from + n; i++) { if (s[i] < '0' || s[i] > '9') return false; v = v * 10 + (uint64_t)(s[i] - '0'); }
            return true;
        }
        static bool allDigits(const std::string& s, size_t from, uint64_t& v) { return s.size() > from && digits(s, from, s.size() - from, v); }
    };

    // Yaesu de texto: FT-991/991A, FT-891, FT-710, FTDX10, FTDX101
    class YaesuEngine : public AsciiCat {
    public:
        int freqDigits = 9;

        bool setFreq(uint64_t hz) override { queue(fmtF("FA", hz)); st_.freq = hz; return true; }
        bool setTxFreq(uint64_t hz) override { queue(fmtF("FB", hz)); st_.txFreq = hz; return true; }
        bool setSplit(bool on) override { queue(on ? "FT3;" : "FT2;"); st_.split = on; return true; }     // FT3 = TX en VFO B
        bool toggleVfo() override { queue("SV;"); st_.vfo = st_.vfo == 'A' ? 'B' : 'A'; return true; }
        // CN00nnn; = tono nnn de la tabla de 50; CT02; = CTCSS solo al emitir; CT00; = apagado
        bool setTone(int dHz) override {
            if (dHz == 0) { queue("CT00;"); st_.tone = 0; return true; }
            const int i = toneIndex(CTCSS50, 50, dHz);
            if (i < 0) return false;
            char b[24]; snprintf(b, sizeof b, "CN00%03d;CT02;", i);
            queue(b); st_.tone = CTCSS50[i]; return true;
        }
        bool setMode(const std::string& mode) override {
            const std::string m = normMode(mode);
            const char* c = nullptr;
            if (m == "LSB") c = "1"; else if (m == "USB") c = "2"; else if (m == "CW") c = "3"; else if (m == "FM") c = "4";
            else if (m == "AM") c = "5"; else if (m == "RTTY") c = "6"; else if (m == "CWR") c = "7"; else if (m == "PKTLSB") c = "8";
            else if (m == "RTTYR") c = "9"; else if (m == "PKTFM") c = "A"; else if (m == "PKTUSB") c = "C";
            if (!c) return false;
            queue(std::string("MD0") + c + ";");
            st_.mode = m;
            return true;
        }

    protected:
        std::string fmtF(const char* p, uint64_t hz) const { char b[32]; snprintf(b, sizeof b, "%s%0*llu;", p, freqDigits, (unsigned long long)hz); return b; }
        bool cmdPtt(bool on) override { queue(on ? "TX1;" : "TX0;"); st_.ptt = on; return true; }
        bool poll(int idx, Item& it) override {
            static const char* const q[] = { "FA;", "MD0;", "TX;", "SM0;", "FB;" };
            if (idx >= 5) return false;
            it.data = q[idx];
            return true;
        }
        void answer(const std::string& r) override {
            uint64_t v;
            if (r.compare(0, 2, "FA") == 0 && allDigits(r, 2, v)) { st_.freq = v; freqDigits = (int)(r.size() - 2); gotReply(); }
            else if (r.compare(0, 2, "FB") == 0 && allDigits(r, 2, v)) { st_.txFreq = v; gotReply(); }
            else if (r.compare(0, 3, "MD0") == 0 && r.size() >= 4) { st_.mode = modeName(r[3]); gotReply(); }
            else if (r.compare(0, 2, "TX") == 0 && r.size() >= 3) { st_.ptt = r[2] != '0'; gotReply(); }
            else if (r.compare(0, 3, "SM0") == 0 && allDigits(r, 3, v)) { st_.smeter = smeterFrom255((int)v); gotReply(); }
            else if (r.compare(0, 2, "FT") == 0 && r.size() >= 3) { st_.split = r[2] == '1' || r[2] == '3'; gotReply(); }
        }
        static std::string modeName(char c) {
            switch (c) {
                case '1': return "LSB"; case '2': return "USB"; case '3': return "CW"; case '4': return "FM"; case '5': return "AM";
                case '6': return "RTTY"; case '7': return "CWR"; case '8': return "PKTLSB"; case '9': return "RTTYR"; case 'A': return "PKTFM";
                case 'B': return "FM"; case 'C': return "PKTUSB"; case 'D': return "AM"; case 'E': return "C4FM";
            }
            return "";
        }
    };

    // Kenwood / Elecraft
    class KenwoodEngine : public AsciiCat {
    public:
        bool setFreq(uint64_t hz) override { queue(fmtF(st_.vfo == 'B' ? "FB" : "FA", hz)); st_.freq = hz; return true; }
        bool setTxFreq(uint64_t hz) override { queue(fmtF(st_.vfo == 'B' ? "FA" : "FB", hz)); st_.txFreq = hz; return true; }
        bool setSplit(bool on) override {
            const int rxv = st_.vfo == 'B' ? 1 : 0, txv = on ? 1 - rxv : rxv;
            char b[16]; snprintf(b, sizeof b, "FR%d;FT%d;", rxv, txv);
            queue(b); st_.split = on; return true;
        }
        int toneBase = 1;           // TN: 01 = 67,0 Hz (TS-2000); 0 en los que cuentan desde 00 (TS-480, TS-590, TS-890)
        // TNnn; = tono de la tabla de 42; TO1; = tono al emitir; TO0; = apagado
        bool setTone(int dHz) override {
            if (dHz == 0) { queue("TO0;"); st_.tone = 0; return true; }
            const int i = toneIndex(CTCSS42, 41, dHz);
            if (i < 0) return false;
            char b[24]; snprintf(b, sizeof b, "TN%02d;TO1;", i + toneBase);
            queue(b); st_.tone = CTCSS42[i]; return true;
        }
        bool toggleVfo() override {
            const int v = st_.vfo == 'B' ? 0 : 1;
            char b[16]; snprintf(b, sizeof b, "FR%d;FT%d;", v, v);
            queue(b); st_.vfo = v ? 'B' : 'A'; return true;
        }
        bool setMode(const std::string& mode) override {
            const std::string m = normMode(mode);
            int c = -1, data = 0;
            if (m == "LSB") c = 1; else if (m == "USB") c = 2; else if (m == "CW") c = 3; else if (m == "FM") c = 4; else if (m == "AM") c = 5;
            else if (m == "RTTY") c = 6; else if (m == "CWR") c = 7; else if (m == "RTTYR") c = 9;
            else if (m == "PKTUSB") { c = 2; data = 1; } else if (m == "PKTLSB") { c = 1; data = 1; } else if (m == "PKTFM") { c = 4; data = 1; }
            if (c < 0) return false;
            char b[16]; snprintf(b, sizeof b, "MD%d;", c);
            queue(b);
            queue(data ? "DA1;" : "DA0;");            // modo datos (TS-590 / TS-890 / TS-990; los demás contestan '?')
            st_.mode = m;
            return true;
        }

    protected:
        std::string fmtF(const char* p, uint64_t hz) const { char b[32]; snprintf(b, sizeof b, "%s%011llu;", p, (unsigned long long)hz); return b; }
        bool cmdPtt(bool on) override { queue(on ? "TX;" : "RX;"); st_.ptt = on; return true; }
        bool poll(int idx, Item& it) override {
            switch (idx) {
                case 0: it.data = "IF;"; return true;
                case 1: it.data = "SM0;"; return true;
                case 2: it.data = st_.vfo == 'B' ? "FA;" : "FB;"; return true;
                case 3: it.data = "DA;"; return true;
            }
            return false;
        }
        bool data_ = false;
        int  modeCode_ = -1;
        void answer(const std::string& r) override {
            uint64_t v;
            if (r.compare(0, 2, "IF") == 0 && r.size() >= 2 + 31) {
                // IF: frecuencia (11), 5 huecos, RIT (5), RIT, XIT, banco, memoria (2), TX/RX [26], modo [27], VFO [28], barrido, split [30]
                const std::string p = r.substr(2);
                if (digits(p, 0, 11, v)) st_.freq = v;
                st_.ptt = p[26] == '1';
                if (p[27] >= '0' && p[27] <= '9') { modeCode_ = p[27] - '0'; refreshMode(); }
                st_.vfo = p[28] == '1' ? 'B' : 'A';
                st_.split = p[30] == '1';
                gotReply();
            } else if ((r.compare(0, 2, "FA") == 0 || r.compare(0, 2, "FB") == 0) && allDigits(r, 2, v)) {
                const bool isA = r[1] == 'A';
                if ((isA && st_.vfo == 'B') || (!isA && st_.vfo != 'B')) st_.txFreq = v; else st_.freq = v;
                gotReply();
            } else if (r.compare(0, 3, "SM0") == 0 && allDigits(r, 3, v)) { st_.smeter = smeterFrom30((int)v); gotReply(); }
            else if (r.compare(0, 2, "SM") == 0 && allDigits(r, 2, v)) { st_.smeter = smeterFrom30((int)v); gotReply(); }
            else if (r.compare(0, 2, "DA") == 0 && r.size() >= 3) { data_ = r[2] == '1'; refreshMode(); gotReply(); }
            else if (r.compare(0, 2, "MD") == 0 && r.size() >= 3) { modeCode_ = r[2] - '0'; refreshMode(); gotReply(); }
        }
        void refreshMode() {
            switch (modeCode_) {
                case 1: st_.mode = data_ ? "PKTLSB" : "LSB"; break;
                case 2: st_.mode = data_ ? "PKTUSB" : "USB"; break;
                case 3: st_.mode = "CW"; break;
                case 4: st_.mode = data_ ? "PKTFM" : "FM"; break;
                case 5: st_.mode = "AM"; break;
                case 6: st_.mode = "RTTY"; break;
                case 7: st_.mode = "CWR"; break;
                case 9: st_.mode = "RTTYR"; break;
            }
        }
    };

    // ---------------------------------------------------------------------------------------------------------------- Yaesu 5 bytes
    class Yaesu5Engine : public CatEngine {
    public:
        Yaesu5Engine() { pollGapMs = 150; cmdGapMs = 60; }
        bool setFreq(uint64_t hz) override {
            const uint64_t v = (hz + 5) / 10;                           // unidades de 10 Hz, 8 cifras BCD, la más significativa primero
            std::string d;
            for (int i = 3; i >= 0; i--) { const uint64_t p = v / ipow(100, i) % 100; d += (char)(((p / 10) << 4) | (p % 10)); }
            d += '\x01';
            queue(d); st_.freq = v * 10; return true;
        }
        bool setTxFreq(uint64_t) override { return false; }            // sin orden para el otro VFO
        bool setSplit(bool on) override { queue(std::string("\0\0\0\0", 4) + (on ? '\x02' : '\x82')); st_.split = on; return true; }
        bool toggleVfo() override { queue(std::string("\0\0\0\0\x81", 5)); st_.vfo = st_.vfo == 'A' ? 'B' : 'A'; return true; }
        // 0B: tono (4 cifras BCD en décimas de Hz, emisión y recepción); 0A con 4A = CTCSS al emitir, 8A = apagado
        bool setTone(int dHz) override {
            if (dHz < 0 || dHz > 2600) return false;
            if (dHz > 0) {
                const char hi = (char)(((dHz / 1000) % 10) << 4 | ((dHz / 100) % 10)), lo = (char)(((dHz / 10) % 10) << 4 | (dHz % 10));
                std::string d; d += hi; d += lo; d += hi; d += lo; d += '\x0B';
                queue(d);
            }
            std::string m; m += dHz > 0 ? '\x4A' : '\x8A'; m += std::string("\0\0\0\x0A", 4);
            queue(m);
            st_.tone = dHz;
            return true;
        }
        bool setMode(const std::string& mode) override {
            const std::string m = normMode(mode);
            int c = -1;
            if (m == "LSB") c = 0x00; else if (m == "USB") c = 0x01; else if (m == "CW") c = 0x02; else if (m == "CWR") c = 0x03;
            else if (m == "AM") c = 0x04; else if (m == "WFM") c = 0x06; else if (m == "FM") c = 0x08;
            else if (m == "PKTUSB" || m == "RTTY") c = 0x0A; else if (m == "PKTFM") c = 0x0C;
            if (c < 0) return false;
            std::string d(5, '\0'); d[0] = (char)c; d[4] = '\x07';
            queue(d); st_.mode = m; return true;
        }
        static uint64_t ipow(uint64_t b, int e) { uint64_t r = 1; while (e-- > 0) r *= b; return r; }

    protected:
        enum { K_FREQ = 1, K_RX = 2, K_TX = 3 };
        bool cmdPtt(bool on) override { queue(std::string("\0\0\0\0", 4) + (on ? '\x08' : '\x88')); st_.ptt = on; return true; }
        bool poll(int idx, Item& it) override {
            switch (idx) {
                case 0: it.data = std::string("\0\0\0\0\x03", 5); it.len = 5; it.kind = K_FREQ; return true;
                case 1: it.data = std::string("\0\0\0\0\xE7", 5); it.len = 1; it.kind = K_RX; return true;
                case 2: it.data = std::string("\0\0\0\0\xF7", 5); it.len = 1; it.kind = K_TX; return true;
            }
            return false;
        }
        void parse(uint32_t) override {
            if (!waiting_ || expectLen_ <= 0) { rx_.clear(); return; }  // las órdenes no contestan
            if ((int)rx_.size() < expectLen_) return;
            const uint8_t* p = (const uint8_t*)rx_.data();
            if (expectKind_ == K_FREQ) {
                uint64_t v = 0;
                for (int i = 0; i < 4; i++) v = v * 100 + (uint64_t)((p[i] >> 4) * 10 + (p[i] & 15));
                st_.freq = v * 10;
                switch (p[4]) {
                    case 0x00: st_.mode = "LSB"; break;   case 0x01: st_.mode = "USB"; break;
                    case 0x02: case 0x82: st_.mode = "CW"; break;  case 0x03: case 0x83: st_.mode = "CWR"; break;
                    case 0x04: case 0x84: st_.mode = "AM"; break;  case 0x06: st_.mode = "WFM"; break;
                    case 0x08: case 0x88: st_.mode = "FM"; break;  case 0x0A: case 0x8A: st_.mode = "PKTUSB"; break;
                    case 0x0C: case 0x8C: st_.mode = "PKTFM"; break;
                }
            } else if (expectKind_ == K_RX) {
                if (!st_.ptt) st_.smeter = p[0] & 0x0F;
            } else if (expectKind_ == K_TX) {
                st_.ptt = (p[0] & 0x80) == 0;
            }
            rx_.erase(0, expectLen_);
            expectLen_ = 0;
            gotReply();
        }
    };


    // ---------------------------------------------------------------------------------------------------------------- red
    // JSON mínimo para los mensajes de SDRconnect (sin dependencias): valor de una clave como texto
    inline std::string jsonField(const std::string& j, const char* key) {
        const std::string k = std::string("\"") + key + "\"";
        size_t p = j.find(k);
        if (p == std::string::npos) return "";
        p = j.find(':', p + k.size());
        if (p == std::string::npos) return "";
        p++;
        while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) p++;
        if (p >= j.size()) return "";
        if (j[p] == '"') {
            std::string v;
            for (size_t i = p + 1; i < j.size() && j[i] != '"'; i++) { if (j[i] == '\\' && i + 1 < j.size()) i++; v += j[i]; }
            return v;
        }
        size_t e = p;
        while (e < j.size() && j[e] != ',' && j[e] != '}' && j[e] != ' ') e++;
        return j.substr(p, e - p);
    }

    // SDRconnect (SDRplay): API WebSocket 1.0.x, JSON {"event_type","property","device","value"}
    class SdrConnectEngine : public CatEngine {
    public:
        std::string host = "127.0.0.1";
        int         port = 5454;
        uint32_t    silenceMs = 6000;          // sin mensajes en este tiempo: enlace perdido

        SdrConnectEngine() { pollGapMs = 300; cmdGapMs = 20; }
        bool receiver() const override { return true; }

        void onConnect(uint32_t now) override {
            reset();
            hs_ = false; skip_ = 0; lastRx_ = now;
            Item it; it.data = "GET / HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Key: QVBSUy1Mb1JhLVJvdG9yIQ==\r\nSec-WebSocket-Version: 13\r\nUser-Agent: APRS-LoRa-Plus-Rotor\r\n\r\n";
            it.reply = true; it.urgent = true; it.kind = K_HS;
            q_.push_back(it);
        }

        bool setFreq(uint64_t hz) override {
            if (!hs_) return false;
            // Fuera de la banda que se ve: mueve también el centro (como la app)
            if (sampleRate_ > 0 && center_ > 0 && (double)llabs((long long)hz - (long long)center_) > sampleRate_ * 0.4) {
                setProp("device_center_frequency", std::to_string((unsigned long long)hz));
                center_ = hz;
            }
            setProp("device_vfo_frequency", std::to_string((unsigned long long)hz));
            st_.freq = hz;
            return true;
        }
        bool setMode(const std::string& mode) override {
            if (!hs_) return false;
            const std::string m = normMode(mode);
            const char* d = nullptr;
            if (m == "FM" || m == "PKTFM") d = "NFM"; else if (m == "WFM") d = "WFM"; else if (m == "AM") d = "AM";
            else if (m == "USB" || m == "PKTUSB") d = "USB"; else if (m == "LSB" || m == "PKTLSB") d = "LSB"; else if (m == "CW" || m == "CWR") d = "CW";
            if (!d) return false;
            setProp("demodulator", d);
            st_.mode = m;
            return true;
        }
        bool setBandwidth(int hz) { if (!hs_) return false; setProp("filter_bandwidth", std::to_string(hz)); return true; }
        bool setTxFreq(uint64_t) override { return false; }
        bool setSplit(bool) override { return false; }
        bool setTone(int) override { return true; }                   // receptor: no hay nada que emitir
        bool toggleVfo() override { return false; }

        // Trama de cliente (siempre con máscara)
        static std::string wsFrame(int opcode, const std::string& payload, uint32_t maskSeed) {
            std::string f;
            f += (char)(0x80 | opcode);
            const size_t n = payload.size();
            if (n < 126) f += (char)(0x80 | n);
            else { f += (char)(0x80 | 126); f += (char)(n >> 8); f += (char)(n & 0xFF); }
            const uint8_t m[4] = { (uint8_t)(maskSeed >> 24), (uint8_t)(maskSeed >> 16), (uint8_t)(maskSeed >> 8), (uint8_t)maskSeed };
            f.append((const char*)m, 4);
            for (size_t i = 0; i < n; i++) f += (char)(payload[i] ^ m[i % 4]);
            return f;
        }
        static std::string msg(const char* type, const std::string& prop, const std::string& value) {
            return std::string("{\"event_type\":\"") + type + "\",\"property\":\"" + prop + "\",\"device\":\"primary\",\"value\":\"" + value + "\"}";
        }
        bool handshaken() const { return hs_; }
        double signalDbm() const { return dbm_; }

    protected:
        enum { K_HS = 9 };
        bool     hs_ = false;
        size_t   skip_ = 0;
        uint32_t lastRx_ = 0, maskSeed_ = 0x5a17c3e1u, now_ = 0;
        uint64_t center_ = 0;
        double   sampleRate_ = 0, dbm_ = -200;

        void send(int opcode, const std::string& p) { maskSeed_ = maskSeed_ * 1664525u + 1013904223u; queue(wsFrame(opcode, p, maskSeed_)); }
        void setProp(const char* prop, const std::string& v) { send(1, msg("set_property", prop, v)); }

        bool cmdPtt(bool) override { return false; }
        bool poll(int idx, Item& it) override {
            if (!hs_) return false;
            static const char* const props[] = { "signal_power", "device_vfo_frequency", "demodulator", "signal_power", "device_center_frequency", "device_sample_rate" };
            if (idx >= 6) return false;
            maskSeed_ = maskSeed_ * 1664525u + 1013904223u;
            it.data = wsFrame(1, msg("get_property", props[idx], ""), maskSeed_);
            it.reply = false;
            return true;
        }
        void parse(uint32_t now) override {
            now_ = now;
            if (!hs_) {
                const size_t e = rx_.find("\r\n\r\n");
                if (e == std::string::npos) return;
                const std::string head = rx_.substr(0, e);
                rx_.erase(0, e + 4);
                if (head.find(" 101") == std::string::npos) { st_.link = false; return; }
                hs_ = true; lastRx_ = now; gotReply();
                // Lo que se lee al conectar
                for (const char* p : { "device_vfo_frequency", "demodulator", "device_center_frequency", "device_sample_rate", "signal_power" }) send(1, msg("get_property", p, ""));
            }
            for (;;) {
                if (skip_) { const size_t k = skip_ < rx_.size() ? skip_ : rx_.size(); rx_.erase(0, k); skip_ -= k; if (skip_) return; }
                if (rx_.size() < 2) return;
                const uint8_t b0 = (uint8_t)rx_[0], b1 = (uint8_t)rx_[1];
                size_t hl = 2;
                uint64_t len = b1 & 0x7F;
                if (len == 126) { if (rx_.size() < 4) return; len = ((uint8_t)rx_[2] << 8) | (uint8_t)rx_[3]; hl = 4; }
                else if (len == 127) { if (rx_.size() < 10) return; len = 0; for (int i = 2; i < 10; i++) len = (len << 8) | (uint8_t)rx_[i]; hl = 10; }
                const bool masked = (b1 & 0x80) != 0;
                if (masked) hl += 4;
                if (rx_.size() < hl) return;
                const int op = b0 & 0x0F;
                if (len > 2048) { rx_.erase(0, hl); skip_ = (size_t)len; lastRx_ = now; continue; }   // espectro, audio...
                if (rx_.size() < hl + len) return;
                std::string p = rx_.substr(hl, (size_t)len);
                if (masked) for (size_t i = 0; i < p.size(); i++) p[i] ^= rx_[hl - 4 + (i % 4)];
                rx_.erase(0, hl + (size_t)len);
                lastRx_ = now;
                if (op == 1) text(p);
                else if (op == 9) send(10, p);                     // ping -> pong
                else if (op == 8) { st_.link = false; hs_ = false; }
            }
        }
        void text(const std::string& j) {
            const std::string prop = jsonField(j, "property"), v = jsonField(j, "value");
            if (prop.empty()) return;
            gotReply();
            if (prop == "device_vfo_frequency") { const double f = atof(v.c_str()); if (f > 0) st_.freq = (uint64_t)llround(f); }
            else if (prop == "device_center_frequency") { const double f = atof(v.c_str()); if (f > 0) center_ = (uint64_t)llround(f); }
            else if (prop == "device_sample_rate") sampleRate_ = atof(v.c_str());
            else if (prop == "demodulator") {
                if (v == "NFM") st_.mode = "FM"; else if (v == "SAM" || v == "DSB") st_.mode = "AM"; else st_.mode = v;
            } else if (prop == "signal_power" && !v.empty()) {
                dbm_ = atof(v.c_str());
                // S9 = -73 dBm (HF), 6 dB por unidad S; por encima, de 10 en 10 dB
                const double d = dbm_ + 73.0;
                int sm = d <= 0 ? (int)lround(9 + d / 6.0) : (int)lround(9 + d / 10.0);
                st_.smeter = sm < 0 ? 0 : (sm > 15 ? 15 : sm);
            }
        }
    public:
        // El transporte llama a esto en cada vuelta: sin mensajes en silenceMs, enlace perdido
        bool stale(uint32_t now) const { return hs_ && now - lastRx_ > silenceMs; }
    };

    // Cliente de rigctld por red (SDR++, gqrx, rigctld de Hamlib...)
    class RigctlClientEngine : public CatEngine {
    public:
        std::string host = "127.0.0.1";
        int         port = 4532;

        RigctlClientEngine() { pollGapMs = 200; replyMs = 800; }
        void onConnect(uint32_t now) override { (void)now; reset(); lines_.clear(); noL_ = noT_ = noI_ = false; }

        bool setFreq(uint64_t hz) override { char b[40]; snprintf(b, sizeof b, "F %llu\n", (unsigned long long)hz); queue(b, true, 0, K_SET); st_.freq = hz; return true; }
        bool setMode(const std::string& mode) override { queue("M " + normMode(mode) + " 0\n", true, 0, K_SET); st_.mode = normMode(mode); return true; }
        bool setTxFreq(uint64_t hz) override { char b[40]; snprintf(b, sizeof b, "I %llu\n", (unsigned long long)hz); queue(b, true, 0, K_SET); st_.txFreq = hz; return true; }
        bool setSplit(bool on) override { queue(on ? "S 1 VFOB\n" : "S 0 VFOA\n", true, 0, K_SET); st_.split = on; return true; }
        bool toggleVfo() override { const char v = st_.vfo == 'A' ? 'B' : 'A'; queue(v == 'B' ? "V VFOB\n" : "V VFOA\n", true, 0, K_SET); st_.vfo = v; return true; }
        bool setTone(int dHz) override {
            if (dHz < 0 || dHz > 2600) return false;
            if (dHz > 0) { char b[24]; snprintf(b, sizeof b, "C %d\n", dHz); queue(b, true, 0, K_SET); }
            queue(dHz > 0 ? "U TONE 1\n" : "U TONE 0\n", true, 0, K_SET);
            st_.tone = dHz;
            return true;
        }

    protected:
        enum { K_F = 1, K_M = 2, K_L = 3, K_T = 4, K_I = 5, K_SET = 6 };
        std::string lines_;
        bool noL_ = false, noT_ = false, noI_ = false;
        std::string mode1_;
        bool cmdPtt(bool on) override { queue(on ? "T 1\n" : "T 0\n", true, 0, K_SET); st_.ptt = on; return true; }
        bool poll(int idx, Item& it) override {
            switch (idx) {
                case 0: it.data = "f\n"; it.kind = K_F; return true;
                case 1: it.data = "m\n"; it.kind = K_M; mode1_.clear(); return true;
                case 2: if (noL_) { it.data = "f\n"; it.kind = K_F; } else { it.data = "l STRENGTH\n"; it.kind = K_L; } return true;
                case 3: if (noT_) return false; it.data = "t\n"; it.kind = K_T; return true;
                case 4: if (noI_) return false; it.data = "i\n"; it.kind = K_I; return true;
            }
            return false;
        }
        void parse(uint32_t) override {
            for (;;) {
                const size_t e = rx_.find('\n');
                if (e == std::string::npos) return;
                std::string l = rx_.substr(0, e);
                rx_.erase(0, e + 1);
                while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
                line(l);
            }
        }
        void line(const std::string& l) {
            if (l.compare(0, 4, "RPRT") == 0) {
                const int code = atoi(l.c_str() + 4);
                if (code != 0) {
                    if (expectKind_ == K_L) noL_ = true;
                    if (expectKind_ == K_T) noT_ = true;
                    if (expectKind_ == K_I) noI_ = true;
                }
                gotReply();
                return;
            }
            switch (expectKind_) {
                case K_F: { const double f = atof(l.c_str()); if (f > 0) st_.freq = (uint64_t)llround(f); gotReply(); break; }
                case K_I: { const double f = atof(l.c_str()); if (f > 0) st_.txFreq = (uint64_t)llround(f); gotReply(); break; }
                case K_M: if (mode1_.empty()) { mode1_ = l.empty() ? "?" : l; st_.mode = normMode(l == "FM" ? "FM" : l); } else gotReply(); break;
                case K_L: { const int db = atoi(l.c_str()); const int sm = db <= 0 ? 9 + (int)lround(db / 6.0) : 9 + (int)lround(db / 10.0); st_.smeter = sm < 0 ? 0 : (sm > 15 ? 15 : sm); gotReply(); break; }
                case K_T: st_.ptt = atoi(l.c_str()) != 0; gotReply(); break;
                default: break;
            }
        }
    };

    // ---------------------------------------------------------------------------------------------------------------- seguimiento
    // Satélite (o cualquier objetivo con Doppler): un cliente (app, tracker) manda la bajada / subida nominales y la velocidad
    // radial; el rotor resintoniza la emisora cuando el cambio pasa de minStep y como mucho cada minMs. Se apaga solo si deja
    // de recibir datos durante timeoutMs.
    class CatTracker {
    public:
        bool        enabled = true;             // botón «Seguir» de la pantalla / orden cattrack on
        bool        active = false;             // hay datos recientes
        std::string name;
        uint64_t    dl = 0, ul = 0;             // nominales (Hz); 0 = no se usa
        double      rr = 0;                     // km/s, + = se aleja
        uint32_t    upd = 0;                    // momento del último dato
        uint32_t    minStep = 50, minMs = 500, timeoutMs = 15000;
        int32_t     doppler = 0;                // Hz aplicados a la bajada

        void update(const std::string& n, uint64_t d, uint64_t u, double rrKmS, uint32_t now) { name = n; dl = d; ul = u; rr = rrKmS; upd = now; active = true; }

        // Llamar a menudo: manda las frecuencias corregidas a la emisora
        void step(Rig& rig, uint32_t now) {
            if (active && now - upd > timeoutMs) { active = false; doppler = 0; }
            if (!active || !enabled || !rig.state().link || now - last_ < minMs) return;
            if (rig.state().ptt && ul == 0) return;
            if (dl) {
                const uint64_t f = dopplerRx(dl, rr);
                doppler = (int32_t)((int64_t)f - (int64_t)dl);
                if (sentDl_ == 0 || llabs((long long)f - (long long)sentDl_) >= (long long)minStep) { rig.setFreq(f); sentDl_ = f; last_ = now; }
            }
            if (ul) {
                const uint64_t f = dopplerTx(ul, rr);
                if (sentUl_ == 0 || llabs((long long)f - (long long)sentUl_) >= (long long)minStep) { if (rig.setTxFreq(f)) sentUl_ = f; last_ = now; }
            }
        }
        void reset() { sentDl_ = sentUl_ = 0; }

    private:
        uint64_t sentDl_ = 0, sentUl_ = 0;
        uint32_t last_ = 0;
    };

    // Motor según el protocolo (lo libera quien lo pide)
    inline CatEngine* makeCat(int proto, uint8_t civAddr) {
        switch (proto) {
            case CAT_CIV: { CivEngine* e = new CivEngine(); e->addr = civAddr; return e; }
            case CAT_YAESU: return new YaesuEngine();
            case CAT_YAESU5: return new Yaesu5Engine();
            case CAT_KENWOOD: return new KenwoodEngine();
            case CAT_SDRCONNECT: return new SdrConnectEngine();
            case CAT_RIGCTL: return new RigctlClientEngine();
        }
        return nullptr;
    }
}
