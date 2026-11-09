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
// Protocolo de control JSON por lineas (docs/CONTROL_PROTOCOL.md): el mismo por BLE, por TCP (puerto 4534) y en la WebUI (POST /api).
// Lo implementa igual el demonio de la Raspberry Pi (raspberrypi/aprs_lora_plus_rotor/api.py) para que la app hable con todos igual.
#include <ArduinoJson.h>
#include <vector>
#include "rc_common.h"
#include "rc_rotctld.h"
#include "rc_cat.h"
#include "rc_wind.h"

namespace RC {

    static const int API_VERSION = 1;
    static const size_t CFG_CHUNK = 180;     // bytes de configuracion por trozo (cabe en un MTU BLE de 247 con el envoltorio JSON)

    class ApiHost {
    public:
        virtual ~ApiHost() {}
        virtual Rotor&      rotor() = 0;
        virtual void        info(JsonObject o) = 0;                         // dev, ver, board, drv, caps...
        virtual void        posExtra(JsonObject o) { (void)o; }             // mecanico, objetivos, fallo, bruto...
        virtual std::string configJson() = 0;                               // configuracion entera (contraseñas incluidas: solo con PIN)
        virtual bool        applyConfig(const std::string& json, std::string& err) = 0;   // valida, guarda; puede reiniciar luego
        virtual bool        calibrate(const char* axis, const char* pt, double deg, std::string& err) = 0;   // pt "a"/"b": toma la lectura actual
        virtual bool        setHere(const char* axis, double deg, std::string& err) = 0;   // corrige el desfase: la posicion actual pasa a ser deg
        virtual void        raw(JsonObject o) = 0;                          // lecturas en bruto de los sensores
        virtual void        clearFault() = 0;
        virtual void        reboot() = 0;
        virtual const char* pin() = 0;                                      // "" = sin PIN
        virtual std::string lastAprs() { return ""; }
        virtual Rig*        rig() { return nullptr; }                       // emisora por CAT (nullptr = sin CAT)
        virtual CatTracker* catTracker() { return nullptr; }
        virtual bool        catPttAllowed() { return false; }
        virtual uint32_t    nowMs() { return 0; }
        virtual WindGuard*  wind() { return nullptr; }                      // proteccion por viento (nullptr = sin ella)
    };

    class ApiSession {
    public:
        bool     authed = false;
        uint32_t subMs  = 0;            // eventos de posicion periodicos (0 = solo al pedirlos)

        // Una linea JSON del cliente -> lineas de respuesta (sin '\n')
        void handle(const std::string& line, ApiHost& h, std::vector<std::string>& out) {
            JsonDocument in;
            if (deserializeJson(in, line) != DeserializationError::Ok || !in["c"].is<const char*>()) { err(out, "", "bad json"); return; }
            const std::string c = in["c"].as<const char*>();
            const bool needPin = h.pin() && h.pin()[0];
            if (c == "auth") {
                const char* p = in["pin"] | "";
                if (!needPin || strcmp(p, h.pin()) == 0) { authed = true; ok(out, c); }
                else { authed = false; err(out, c, "bad pin"); }
                return;
            }
            if (c == "info") { info(h, out); return; }
            if (c == "get")  { out.push_back(posEvent(h)); return; }
            if (c == "raw")  { JsonDocument d; d["e"] = "raw"; h.raw(d.as<JsonObject>()); out.push_back(dump(d)); return; }
            if (c == "sub")  { subMs = in["ms"] | 1000; if (subMs != 0 && subMs < 200) subMs = 200; ok(out, c); return; }
            if (c == "cat")  { const std::string e = catEvent(h); if (e.empty()) err(out, c, "no cat"); else out.push_back(e); return; }
            if (needPin && !authed) { err(out, c, "auth required"); return; }

            Rotor& r = h.rotor();
            if (c == "pos") {
                if (!in["az"].is<double>()) { err(out, c, "az"); return; }
                const double az = in["az"].as<double>();
                bool okk;
                if (in["el"].is<double>() && r.hasEl()) {
                    double el = in["el"].as<double>();
                    if (el < r.minEl()) el = r.minEl();
                    if (el > r.maxEl()) el = r.maxEl();
                    okk = r.setPos(az, el);
                } else okk = r.setAz(az);
                if (okk) ok(out, c); else err(out, c, "rejected");
                return;
            }
            if (c == "stop")  { r.stop(); ok(out, c); return; }
            if (c == "park")  { if (r.park()) ok(out, c); else err(out, c, "rejected"); return; }
            if (c == "clear") { h.clearFault(); ok(out, c); return; }
            if (c == "move") {
                const int dir = parseMoveDir(in["d"] | "");
                if (!dir) { err(out, c, "d"); return; }
                if (r.move(dir, in["s"] | 0)) ok(out, c); else err(out, c, "rejected");
                return;
            }
            if (c == "cal") {
                std::string e;
                if (h.calibrate(in["axis"] | "az", in["pt"] | "a", in["deg"] | 0.0, e)) ok(out, c); else err(out, c, e.c_str());
                return;
            }
            if (c == "here") {
                std::string e;
                if (h.setHere(in["axis"] | "az", in["deg"] | 0.0, e)) ok(out, c); else err(out, c, e.c_str());
                return;
            }
            if (c == "cfgget") {
                const std::string cfg = h.configJson();
                const size_t n = (cfg.size() + CFG_CHUNK - 1) / CFG_CHUNK;
                for (size_t i = 0; i < n || i == 0; i++) {
                    JsonDocument d;
                    d["e"] = "cfg"; d["i"] = (int)i; d["n"] = (int)(n ? n : 1);
                    d["d"] = cfg.substr(i * CFG_CHUNK, CFG_CHUNK);
                    out.push_back(dump(d));
                    if (n == 0) break;
                }
                return;
            }
            if (c == "cfgset") {
                const int i = in["i"] | -1, n = in["n"] | 0;
                const char* d = in["d"] | "";
                if (i < 0 || n <= 0 || n > 200 || i >= n) { err(out, c, "i/n"); return; }
                if (i == 0) { cfgIn_.clear(); cfgNext_ = 0; }
                if (i != cfgNext_) { err(out, c, "order"); cfgNext_ = 0; cfgIn_.clear(); return; }
                cfgIn_ += d;
                cfgNext_++;
                if (cfgIn_.size() > 16384) { err(out, c, "too big"); cfgNext_ = 0; cfgIn_.clear(); return; }
                if (i + 1 < n) { JsonDocument a; a["e"] = "ok"; a["c"] = c; a["i"] = i; out.push_back(dump(a)); return; }
                std::string e;
                const bool good = h.applyConfig(cfgIn_, e);
                cfgIn_.clear(); cfgNext_ = 0;
                if (!good) { err(out, c, e.c_str()); return; }
                JsonDocument a; a["e"] = "ok"; a["c"] = c; a["i"] = i; a["done"] = true; out.push_back(dump(a));
                return;
            }
            if (c == "catset") {
                Rig* g = h.rig();
                if (!g) { err(out, c, "no cat"); return; }
                bool good = true;
                if (in["f"].is<double>()) { const double f = in["f"].as<double>(); good &= f > 0 && f < 1e11 && g->setFreq((uint64_t)llround(f)); }
                if (in["m"].is<const char*>()) good &= g->setMode(in["m"].as<const char*>());
                if (in["txf"].is<double>()) { const double f = in["txf"].as<double>(); good &= f > 0 && f < 1e11 && g->setTxFreq((uint64_t)llround(f)); }
                if (in["split"].is<bool>()) good &= g->setSplit(in["split"].as<bool>());
                if (in["tone"].is<double>()) { const double t = in["tone"].as<double>(); good &= t >= 0 && t <= 260 && g->setTone((int)lround(t * 10)); }
                if (in["vfo"].is<const char*>()) {
                    const std::string v = in["vfo"].as<const char*>();
                    good &= v == "swap" ? g->toggleVfo() : g->setVfo(v == "B" ? 'B' : 'A');
                }
                if (in["ptt"].is<bool>()) {
                    const bool on = in["ptt"].as<bool>();
                    if (on && !h.catPttAllowed()) { err(out, c, "ptt not allowed"); return; }
                    good &= g->setPtt(on);
                }
                if (good) ok(out, c); else err(out, c, "rejected");
                return;
            }
            if (c == "cattrack") {
                CatTracker* t = h.catTracker();
                if (!t) { err(out, c, "no cat"); return; }
                if (in["on"].is<bool>()) { t->enabled = in["on"].as<bool>(); t->reset(); }
                if (in["dl"].is<double>() || in["ul"].is<double>()) {
                    t->update(in["name"] | "", (uint64_t)llround(in["dl"] | 0.0), (uint64_t)llround(in["ul"] | 0.0), in["rr"] | 0.0, h.nowMs());
                }
                ok(out, c);
                return;
            }
            if (c == "wx") {                                    // tiempo de internet de la app: ws / wg m/s, wd grados, age s, src
                WindGuard* g = h.wind();
                if (!g) { err(out, c, "no wind"); return; }
                if (!in["ws"].is<double>() && !in["wg"].is<double>()) { err(out, c, "ws"); return; }
                const double ws = in["ws"] | 0.0, wg = in["wg"].is<double>() ? in["wg"].as<double>() : -1.0;
                if (ws < 0 || ws > 150 || wg > 150) { err(out, c, "range"); return; }
                const long age = in["age"] | 0L;
                if (g->update(ws, wg, in["wd"] | -1, (uint32_t)(age < 0 ? 0 : age > 86400 ? 86400 : age), in["src"] | "", h.nowMs())) {
                    r.stop(); r.park();
                }
                ok(out, c);
                return;
            }
            if (c == "reboot") { ok(out, c); h.reboot(); return; }
            err(out, c, "unknown");
        }

        static std::string posEvent(ApiHost& h) {
            Rotor& r = h.rotor();
            double az, el;
            r.getPos(az, el);
            JsonDocument d;
            d["e"] = "pos";
            d["az"] = round1(az);
            if (r.hasEl()) d["el"] = round1(el);
            const uint32_t st = r.status();
            d["st"] = st;
            d["mv"] = (st & ST_MOVING) != 0;
            h.posExtra(d.as<JsonObject>());
            windInfo(h, d);
            return dump(d);
        }

        // "wind":{"ws","wg","wd","pk","age","src","lk":aparcado,"hold":s hasta poder liberar,"on":proteccion activa,"park":m/s}
        static void windInfo(ApiHost& h, JsonDocument& d) {
            WindGuard* g = h.wind();
            if (!g || (!g->have() && !g->cfg.on)) return;
            JsonObject w = d["wind"].to<JsonObject>();
            w["on"] = g->cfg.on;
            w["park"] = g->cfg.parkMs;
            w["lk"] = g->locked();
            if (!g->have()) return;
            const uint32_t now = h.nowMs();
            w["ws"] = round1(g->wind());
            if (g->gust() >= 0) w["wg"] = round1(g->gust());
            if (g->dir() >= 0) w["wd"] = g->dir();
            w["pk"] = round1(g->peak());
            w["age"] = g->ageS(now);
            if (g->src()[0]) w["src"] = g->src();
            if (g->locked()) w["hold"] = g->holdLeftS(now);
        }

        // Estado de la emisora: {"e":"cat","l":enlace,"f":Hz,"m":modo,"tf":Hz,"sp":split,"v":"A","ptt":..,"sm":0-15,"tn":CTCSS Hz,"pa":ptt permitido,
        //   "tr":{"en":seguir,"on":con datos,"n":nombre,"dop":Hz}}
        static std::string catEvent(ApiHost& h) {
            Rig* g = h.rig();
            if (!g) return "";
            const CatState& s = g->state();
            JsonDocument d;
            d["e"] = "cat";
            d["rig"] = g->info();
            d["l"] = s.link;
            putHz(d["f"], s.freq);
            d["m"] = s.mode;
            if (s.txFreq) putHz(d["tf"], s.txFreq);
            d["sp"] = s.split;
            d["v"] = std::string(1, s.vfo);
            d["ptt"] = s.ptt;
            d["sm"] = s.smeter;
            if (s.tone > 0) d["tn"] = s.tone / 10.0;
            d["pa"] = h.catPttAllowed();
            if (CatTracker* t = h.catTracker()) {
                JsonObject tr = d["tr"].to<JsonObject>();
                tr["en"] = t->enabled; tr["on"] = t->active; tr["n"] = t->name; tr["dop"] = t->doppler;
            }
            return dump(d);
        }

        static void info(ApiHost& h, std::vector<std::string>& out) {
            JsonDocument d;
            d["e"] = "info";
            d["api"] = API_VERSION;
            Rotor& r = h.rotor();
            JsonObject lim = d["lim"].to<JsonObject>();
            lim["minAz"] = r.minAz(); lim["maxAz"] = r.maxAz();
            lim["minEl"] = r.minEl(); lim["maxEl"] = r.maxEl();
            d["hasEl"] = r.hasEl();
            d["pin"] = h.pin() && h.pin()[0];
            h.info(d.as<JsonObject>());
            d["cat"] = h.rig() != nullptr;
            d["wind"] = h.wind() != nullptr;
            out.push_back(dump(d));
        }

        // Hz como entero (hasta 4,29 GHz cabe en 32 bits en todas las placas)
        template <typename V> static void putHz(V v, uint64_t hz) { if (hz <= 0xFFFFFFFFull) v.set((uint32_t)hz); else v.set((double)hz); }

        static double round1(double v) { return round(v * 10.0) / 10.0; }

        static std::string dump(const JsonDocument& d) { std::string s; serializeJson(d, s); return s; }

    private:
        std::string cfgIn_;
        int         cfgNext_ = 0;

        static void ok(std::vector<std::string>& out, const std::string& c) {
            JsonDocument d; d["e"] = "ok"; d["c"] = c; out.push_back(dump(d));
        }
        static void err(std::vector<std::string>& out, const std::string& c, const char* m) {
            JsonDocument d; d["e"] = "err"; if (!c.empty()) d["c"] = c; d["m"] = m; out.push_back(dump(d));
        }
    };

}
