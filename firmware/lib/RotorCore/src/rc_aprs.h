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
// Control del rotor por mensajes APRS (LoRa APRS o cualquier TNC por KISS). Ver docs/APRS_CONTROL.md.
//   Ordenes (texto del mensaje, sin distinguir mayusculas):  P az el | AZ az [EL el] | PARK | STOP | ? | HELP   [+ codigo TOTP]
//   Respuesta: ack (si el mensaje lleva numero) y un mensaje con la posicion: "AZ 123 EL 45 OK".
// Autorizacion: lista de indicativos permitidos y, opcionalmente, codigo TOTP de 6 cifras (RFC 6238, HMAC-SHA1, 30 s) que no se
// puede reutilizar. El codigo autentica pero no oculta el contenido (lo permiten las normas de radioaficionado).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "rc_common.h"

namespace RC {

    // ---------------------------------------------------------------------------------------------------------------
    // SHA-1 / HMAC-SHA1 / TOTP (sin dependencias)
    // ---------------------------------------------------------------------------------------------------------------
    namespace Crypto {
        struct Sha1 {
            uint32_t h[5];
            uint64_t len;
            uint8_t  buf[64];
            size_t   n;
            static uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }
            Sha1() { h[0] = 0x67452301; h[1] = 0xEFCDAB89; h[2] = 0x98BADCFE; h[3] = 0x10325476; h[4] = 0xC3D2E1F0; len = 0; n = 0; }
            void block(const uint8_t* p) {
                uint32_t w[80];
                for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
                for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
                for (int i = 0; i < 80; i++) {
                    uint32_t f, k;
                    if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
                    else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
                    else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
                    else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
                    const uint32_t t = rol(a, 5) + f + e + k + w[i];
                    e = d; d = c; c = rol(b, 30); b = a; a = t;
                }
                h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
            }
            void update(const uint8_t* p, size_t l) {
                len += l;
                while (l--) { buf[n++] = *p++; if (n == 64) { block(buf); n = 0; } }
            }
            void final(uint8_t out[20]) {
                const uint64_t bits = len * 8;
                uint8_t pad = 0x80;
                update(&pad, 1);
                pad = 0;
                while (n != 56) update(&pad, 1);
                uint8_t lb[8];
                for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
                update(lb, 8);
                for (int i = 0; i < 5; i++) { out[4 * i] = h[i] >> 24; out[4 * i + 1] = h[i] >> 16; out[4 * i + 2] = h[i] >> 8; out[4 * i + 3] = h[i]; }
            }
        };

        inline void hmacSha1(const uint8_t* key, size_t kl, const uint8_t* msg, size_t ml, uint8_t out[20]) {
            uint8_t k[64] = {0};
            if (kl > 64) { Sha1 s; s.update(key, kl); s.final(k); } else memcpy(k, key, kl);
            uint8_t ip[64], op[64];
            for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
            uint8_t inner[20];
            { Sha1 s; s.update(ip, 64); s.update(msg, ml); s.final(inner); }
            { Sha1 s; s.update(op, 64); s.update(inner, 20); s.final(out); }
        }

        // Base32 (RFC 4648, el formato de las apps de autenticacion). Devuelve la longitud o -1
        inline int base32(const char* s, uint8_t* out, size_t max) {
            uint32_t acc = 0;
            int bits = 0;
            size_t n = 0;
            for (; *s; s++) {
                char c = (char)toupper((unsigned char)*s);
                int v;
                if (c >= 'A' && c <= 'Z') v = c - 'A';
                else if (c >= '2' && c <= '7') v = c - '2' + 26;
                else if (c == '=' || c == ' ' || c == '-') continue;
                else return -1;
                acc = (acc << 5) | (uint32_t)v;
                bits += 5;
                if (bits >= 8) {
                    if (n >= max) return -1;
                    out[n++] = (uint8_t)(acc >> (bits - 8));
                    bits -= 8;
                }
            }
            return (int)n;
        }

        inline uint32_t hotp(const uint8_t* key, size_t kl, uint64_t counter, int digits = 6) {
            uint8_t msg[8];
            for (int i = 7; i >= 0; i--) { msg[i] = (uint8_t)(counter & 0xff); counter >>= 8; }
            uint8_t h[20];
            hmacSha1(key, kl, msg, 8, h);
            const int o = h[19] & 0x0f;
            uint32_t bin = ((uint32_t)(h[o] & 0x7f) << 24) | ((uint32_t)h[o + 1] << 16) | ((uint32_t)h[o + 2] << 8) | h[o + 3];
            uint32_t mod = 1;
            for (int i = 0; i < digits; i++) mod *= 10;
            return bin % mod;
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Tramas APRS en formato TNC2 ("SRC>DEST,PATH:info")
    // ---------------------------------------------------------------------------------------------------------------
    namespace Aprs {

        // LoRa APRS lleva delante "<\xff\x01"
        inline std::string stripLoraHeader(const std::string& s) {
            if (s.size() >= 3 && s[0] == '<' && (uint8_t)s[1] == 0xff && s[2] == 0x01) return s.substr(3);
            return s;
        }

        inline std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
        inline std::string trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && isspace((unsigned char)s[a])) a++;
            while (b > a && isspace((unsigned char)s[b - 1])) b--;
            return s.substr(a, b - a);
        }
        inline std::string baseCall(const std::string& c) { const size_t p = c.find('-'); return upper(p == std::string::npos ? c : c.substr(0, p)); }

        struct Message {
            std::string src, dest, path, addressee, text, id;
        };

        // Interpreta un mensaje APRS (":ADDRESSEE:texto{id"). false si la trama no es un mensaje
        inline bool parseMessage(const std::string& frame0, Message& m) {
            const std::string frame = stripLoraHeader(frame0);
            const size_t gt = frame.find('>');
            const size_t colon = frame.find(':');
            if (gt == std::string::npos || colon == std::string::npos || gt > colon || gt == 0) return false;
            m.src = upper(frame.substr(0, gt));
            const std::string hdr = frame.substr(gt + 1, colon - gt - 1);
            const size_t comma = hdr.find(',');
            m.dest = hdr.substr(0, comma);
            m.path = comma == std::string::npos ? "" : hdr.substr(comma + 1);
            std::string info = frame.substr(colon + 1);
            // Trafico de terceros "}SRC>DEST,PATH:..." (iGate): se desenvuelve una vez
            if (!info.empty() && info[0] == '}') return parseMessage(info.substr(1), m);
            if (info.size() < 11 || info[0] != ':' || info[10] != ':') return false;
            m.addressee = upper(trim(info.substr(1, 9)));
            std::string body = info.substr(11);
            while (!body.empty() && (body.back() == '\r' || body.back() == '\n')) body.pop_back();
            m.id.clear();
            const size_t br = body.rfind('{');
            if (br != std::string::npos) {
                std::string id = body.substr(br + 1);
                const size_t close = id.find('}');           // formato de respuesta-ack "{MM}AA"
                if (close != std::string::npos) id = id.substr(0, close);
                if (id.size() >= 1 && id.size() <= 5) { m.id = id; body = body.substr(0, br); }
            }
            m.text = body;
            return true;
        }

        inline std::string pad9(const std::string& s) { std::string r = s.substr(0, 9); while (r.size() < 9) r += ' '; return r; }

        inline std::string buildMessage(const std::string& src, const std::string& tocall, const std::string& path,
                                        const std::string& to, const std::string& text, const std::string& id = "") {
            std::string f = src + ">" + tocall;
            if (!path.empty()) f += "," + path;
            f += "::" + pad9(to) + ":" + text.substr(0, 67);
            if (!id.empty()) f += "{" + id;
            return f;
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Interprete de ordenes APRS del rotor
    // ---------------------------------------------------------------------------------------------------------------
    struct AprsCtlCfg {
        std::string callsign = "N0CALL-12";     // indicativo del rotor (destinatario de los mensajes)
        std::string tocall   = "APZLRR";        // destino de las tramas que manda (rango experimental APZ)
        std::string path;                       // ruta de las respuestas (vacio = directo)
        std::string allow;                      // indicativos permitidos separados por comas o espacios ("EA7JRS" = cualquier SSID;
                                                // "EA7JRS-9" = solo ese). Vacio = el indicativo base del rotor
        std::string totpSecret;                 // secreto base32; vacio = sin codigo
        bool        reply = true;               // contestar con la posicion
    };

    class AprsControl {
    public:
        AprsCtlCfg cfg;

        // Trama recibida (TNC2). now = ms; utc = hora UTC (0 = desconocida). Las tramas a enviar se añaden a tx.
        // Devuelve true si la trama era para el rotor.
        bool onFrame(const std::string& frame, uint32_t now, uint64_t utc, Rotor& rot, std::string* tx, size_t txMax, size_t& txN) {
            txN = 0;
            Aprs::Message m;
            if (!Aprs::parseMessage(frame, m)) return false;
            if (m.addressee != Aprs::upper(cfg.callsign)) return false;
            if (m.text.compare(0, 3, "ack") == 0 || m.text.compare(0, 3, "rej") == 0) return true;   // acks de nuestras respuestas
            const std::string& me = cfg.callsign;
            auto push = [&](const std::string& f) { if (txN < txMax) tx[txN++] = f; };

            // Repetido (el remitente reintenta hasta recibir el ack): se confirma otra vez sin ejecutar
            if (!m.id.empty() && seen(m.src, m.id, now)) {
                push(Aprs::buildMessage(me, cfg.tocall, cfg.path, m.src, "ack" + m.id));
                return true;
            }
            if (!allowed(m.src)) {
                lastResult_ = "denied " + m.src;
                if (!m.id.empty()) push(Aprs::buildMessage(me, cfg.tocall, cfg.path, m.src, "rej" + m.id));
                return true;
            }
            std::string t[8];
            const size_t k = split(Aprs::upper(Aprs::trim(m.text)), t, 8);
            if (k == 0) return true;
            size_t n = k;
            // Codigo TOTP: ultimo elemento de 6 cifras
            if (!cfg.totpSecret.empty()) {
                if (!checkCode(t[n - 1], utc)) {
                    lastResult_ = utc ? "bad code " + m.src : "no time for code";
                    if (!m.id.empty()) push(Aprs::buildMessage(me, cfg.tocall, cfg.path, m.src, "rej" + m.id));
                    if (cfg.reply) push(Aprs::buildMessage(me, cfg.tocall, cfg.path, m.src, utc ? "ERR code" : "ERR no time"));
                    return true;
                }
                n--;
            }
            if (!m.id.empty()) { remember(m.src, m.id, now); push(Aprs::buildMessage(me, cfg.tocall, cfg.path, m.src, "ack" + m.id)); }

            std::string res = execute(t, n, rot);
            lastResult_ = m.src + ": " + Aprs::trim(m.text) + " -> " + res;
            if (cfg.reply) push(Aprs::buildMessage(me, cfg.tocall, cfg.path, m.src, res));
            return true;
        }

        // Texto de estado para respuestas y balizas: "AZ 123 EL 45 OK"
        static std::string statusLine(Rotor& rot, const char* tail) {
            double az, el;
            rot.getPos(az, el);
            char b[64];
            if (rot.hasEl()) snprintf(b, sizeof(b), "AZ %.0f EL %.0f %s", az, el, tail);
            else snprintf(b, sizeof(b), "AZ %.0f %s", az, tail);
            return b;
        }

        const std::string& lastResult() const { return lastResult_; }

        bool allowed(const std::string& src) const {
            const std::string s = Aprs::upper(src);
            std::string list = cfg.allow.empty() ? Aprs::baseCall(cfg.callsign) : Aprs::upper(cfg.allow);
            for (auto& c : list) if (c == ',' || c == ';') c = ' ';
            std::string t[24];
            const size_t k = split(list, t, 24);
            for (size_t i = 0; i < k; i++) {
                if (t[i].find('-') == std::string::npos) { if (Aprs::baseCall(s) == t[i]) return true; }
                else if (s == t[i]) return true;
            }
            return false;
        }

        // Codigo valido en la ventana de +-30 s y no usado antes (sin reutilizacion)
        bool checkCode(const std::string& code, uint64_t utc) {
            if (utc < 1600000000ULL || code.size() != 6) return false;
            for (char c : code) if (!isdigit((unsigned char)c)) return false;
            uint8_t key[64];
            const int kl = Crypto::base32(cfg.totpSecret.c_str(), key, sizeof(key));
            if (kl <= 0) return false;
            const uint32_t want = (uint32_t)atoi(code.c_str());
            const uint64_t step = utc / 30;
            for (int d = -1; d <= 1; d++) {
                const uint64_t c = step + d;
                if (c <= lastCounter_) continue;
                if (Crypto::hotp(key, (size_t)kl, c) == want) { lastCounter_ = c; return true; }
            }
            return false;
        }

    private:
        struct Seen { std::string src, id; uint32_t t = 0; };
        Seen        seen_[8];
        size_t      seenPos_ = 0;
        uint64_t    lastCounter_ = 0;
        std::string lastResult_;

        bool seen(const std::string& s, const std::string& id, uint32_t now) const {
            for (auto& e : seen_) if (!e.src.empty() && e.src == s && e.id == id && now - e.t < 180000) return true;
            return false;
        }
        void remember(const std::string& s, const std::string& id, uint32_t now) {
            seen_[seenPos_] = Seen{ s, id, now };
            seenPos_ = (seenPos_ + 1) % 8;
        }

        static bool num(const std::string& s, double& v) {
            std::string x = s;
            for (auto& c : x) if (c == ',') c = '.';
            return number(x, v);
        }

        std::string execute(std::string* t, size_t n, Rotor& rot) {
            double az, el;
            const std::string& c = t[0];
            if (c == "?" || c == "POS" || c == "STATUS" || c == "Q") return statusLine(rot, statusTail(rot));
            if (c == "STOP" || c == "S") { rot.stop(); return statusLine(rot, "STOP"); }
            if (c == "PARK" || c == "K") { return rot.park() ? statusLine(rot, "PARK") : std::string("ERR park"); }
            if (c == "HELP" || c == "H") return "P az el|AZ az EL el|PARK|STOP|?";
            if (c == "P" && n >= 2 && num(t[1], az)) {
                el = 0;
                if (n >= 3 && !num(t[2], el)) return "ERR el";
                return goTo(rot, az, el, n >= 3);
            }
            if (c.size() > 1 && c[0] == 'P' && num(c.substr(1), az)) {        // "P123 45"
                el = 0;
                if (n >= 2 && !num(t[1], el)) return "ERR el";
                return goTo(rot, az, el, n >= 2);
            }
            if (c == "AZ" && n >= 2 && num(t[1], az)) {                      // "AZ 123 EL 45"
                bool hasEl = false;
                el = 0;
                if (n >= 4 && t[2] == "EL") { if (!num(t[3], el)) return "ERR el"; hasEl = true; }
                return goTo(rot, az, el, hasEl);
            }
            return "ERR cmd (HELP)";
        }

        static const char* statusTail(Rotor& rot) { return (rot.status() & ST_MOVING) ? "MOVING" : "OK"; }

        static std::string goTo(Rotor& rot, double az, double el, bool hasEl) {
            if (az < -360 || az > 720 || el < -10 || el > 180) return "ERR range";
            bool ok;
            if (hasEl && rot.hasEl()) {
                if (el < rot.minEl()) el = rot.minEl();
                if (el > rot.maxEl()) el = rot.maxEl();
                ok = rot.setPos(az, el);
            } else {
                ok = rot.setAz(az);
            }
            if (!ok) return "ERR rejected";
            char b[48];
            if (hasEl && rot.hasEl()) snprintf(b, sizeof(b), "GO AZ %.0f EL %.0f", wrap360(az), el);
            else snprintf(b, sizeof(b), "GO AZ %.0f", wrap360(az));
            return b;
        }
    };

}
