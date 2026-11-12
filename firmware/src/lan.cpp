/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "lan.h"
#include "board.h"
#include "config.h"

#if ROTOR_HAS_LAN

#include <algorithm>
#include <cmath>
#include "aprs_net.h"
#include "drivers.h"
#include "nmea.h"
#include "sk_link.h"
#include "store.h"
#include "stream.h"
#include "tsync.h"

using namespace plotter;

namespace Lan {

    static bool       on = false;
    static DataStore  store;
    static Nmea0183   tcpNmea, udpNmea;
    static LineBuffer tcpLine, udpLine;
    static uint32_t   tcpLines = 0, udpLines = 0, skValues = 0, lastMs = 0, wxMs = 0;
    static const double KN_MS = 1852.0 / 3600.0;

    void begin() {
        on = Cfg.wifiMode != 0 && (Cfg.lanKind != 0 || Cfg.lanUdp > 0 || !Cfg.owmKey.empty());
        if (!on) return;
        net::Config c;
        c.ownWifi = false;                       // la WiFi es la del rotor (src/net.cpp)
        snprintf(c.host, sizeof c.host, "%s", Cfg.lanHost.c_str());
        c.tcpKind = Cfg.lanKind;
        c.tcpPort = Cfg.lanPort;
        c.udpPort = Cfg.lanUdp;
        snprintf(c.owmKey, sizeof c.owmKey, "%s", Cfg.owmKey.c_str());
        net::begin();
        net::apply(c);
        logf("red local: servidor %d %s:%d, UDP %d, OpenWeatherMap %s\n", Cfg.lanKind, Cfg.lanHost.empty() ? "(puerta de enlace)" : Cfg.lanHost.c_str(),
             Cfg.lanPort, Cfg.lanUdp, Cfg.owmKey.empty() ? "no" : "si");
    }

    static void readLines(uint32_t now) {
        static uint8_t buf[512];
        if (Cfg.lanKind == net::TCP_SIGNALK) {                  // Signal K: una linea JSON por mensaje
            static char sk[2048];
            static int len = 0;
            static bool over = false;
            for (int round = 0; round < 4; round++) {
                const int n = net::readTcp(buf, sizeof buf);
                if (n <= 0) break;
                for (int i = 0; i < n; i++) {
                    const char ch = (char)buf[i];
                    if (ch == '\n') {
                        if (!over && len > 0) {
                            sk[len] = 0;
                            sk::Notice nt;
                            skValues += (uint32_t)sk::feedLine(sk, now, store, nt);
                            tcpLines++;
                            lastMs = now;
                        }
                        len = 0;
                        over = false;
                    } else if (ch != '\r') {
                        if (len < (int)sizeof sk - 1) sk[len++] = ch; else over = true;
                    }
                }
            }
        }
        for (int pass = Cfg.lanKind == net::TCP_SIGNALK ? 1 : 0; pass < 2; pass++) {
            LineBuffer& lb = pass ? udpLine : tcpLine;
            Nmea0183& rd = pass ? udpNmea : tcpNmea;
            for (int round = 0; round < 4; round++) {
                const int n = pass ? net::readUdp(buf, sizeof buf) : net::readTcp(buf, sizeof buf);
                if (n <= 0) break;
                for (int i = 0; i < n; i++) {
                    if (!lb.push((char)buf[i])) continue;
                    (pass ? udpLines : tcpLines)++;
                    lastMs = now;
                    tsync::offerNmea(lb.line());                       // hora del GPS (gpsd, el movil, el plotter...)
                    rd.feed(lb.line(), Source::Tcp, now, store);     // el AIS no se usa aqui
                }
            }
        }
        net::Weather w;
        if (net::takeWeather(w)) {
            auto put = [&](Key k, double v) { if (!std::isnan(v)) store.set(k, v, Source::Internet, now, Quality::Estimated); };
            put(Key::Tws, w.windMs / KN_MS);
            put(Key::Twd, w.windDeg);
            put(Key::Gust, w.gustMs / KN_MS);
            wxMs = now ? now : 1;
        }
    }

    bool position(double& lat, double& lon) {
        const uint32_t now = millis();
        if (on && Cfg.lanPos && store.get(Key::Lat, now, lat) && store.get(Key::Lon, now, lon)) return true;
        lat = Cfg.lat; lon = Cfg.lon;
        return Cfg.lat != 0 || Cfg.lon != 0;
    }

    // Viento a la proteccion: el mayor de aparente y real (en un barco, lo que llega a la antena es el aparente), cada segundo.
    // Medido por la red manda sobre el tiempo de la app; el anemómetro de la caja, sobre todo.
    static void wind(uint32_t now) {
        RC::WindGuard& g = Drv::wind();
        double aws = NAN, tws = NAN, gust = NAN, twd = NAN;
        store.get(Key::Aws, now, aws);
        store.get(Key::Tws, now, tws);
        store.get(Key::Gust, now, gust);
        store.get(Key::Twd, now, twd);
        const bool haveA = !std::isnan(aws), haveT = !std::isnan(tws);
        const bool measured = haveA || (haveT && store.source(Key::Tws, now) != Source::Internet);
        Drv::setAnemoLocal(Cfg.lanWind && measured);
        if (!Cfg.lanWind || Cfg.anemoPin >= 0 || (!haveA && !haveT)) return;
        const double kn = haveA && haveT ? std::max(aws, tws) : haveA ? aws : tws;
        const char* src = measured ? (Cfg.lanKind == net::TCP_SIGNALK ? "signalk" : "lan") : "owm";
        const uint32_t ageS = measured ? 0 : store.ageMs(Key::Tws, now) / 1000;
        if (g.update(kn * KN_MS, std::isnan(gust) ? -1.0 : gust * KN_MS, std::isnan(twd) ? -1 : (int)lround(twd), ageS, src, now)) {
            Drv::rotor().stop();
            Drv::rotor().park();
        }
    }

    void loop() {
        if (!on) return;
        const uint32_t now = millis();
        readLines(now);
        static uint32_t last = 0;
        if (now - last < 1000) return;
        last = now;
        double la, lo;
        if (position(la, lo)) net::setPosition(la, lo);         // para pedir el tiempo de ese punto
        RC::HeadingRotor& h = Drv::heading();
        double hdg;                                             // verdadero (HDT, VHW, Signal K) o magnetico + declinacion (HDG / HDM)
        if (store.get(Key::Heading, now, hdg)) h.setHeading(hdg, now);
        else if (store.get(Key::HeadingMag, now, hdg)) h.setHeading(hdg + Cfg.headVar, now);
        else h.tick(now);
        wind(now);
    }

    void info(JsonObject o) {
        const tsync::Status ts = tsync::status();                 // reloj: quien lo puso (GPS de la red o NTP)
        JsonObject tm = o["clock"].to<JsonObject>();
        tm["src"] = plotter::timeSourceName(ts.source);
        if (ts.source != plotter::TimeSource::None) tm["age"] = ts.ageS;
        if (ts.ntp) tm["ntp"] = ts.ntpServer;
        if (!on) return;
        const net::Status& s = net::status();
        JsonObject l = o["lan"].to<JsonObject>();
        l["kind"] = Cfg.lanKind;
        l["wifi"] = s.wifi;
        l["tcp"] = s.tcp;
        l["lines"] = tcpLines;
        if (Cfg.lanKind == net::TCP_SIGNALK) l["sk"] = skValues;
        if (Cfg.lanUdp) l["udp"] = udpLines;
        const uint32_t now = millis();
        if (lastMs) l["age"] = (now - lastMs) / 1000;
        if (!Cfg.owmKey.empty()) { l["wxOk"] = s.wxOk; l["wxFail"] = s.wxFail; if (wxMs) l["wxAge"] = (now - wxMs) / 1000; }
        double la, lo;
        if (store.get(Key::Lat, now, la) && store.get(Key::Lon, now, lo)) { l["lat"] = round(la * 1e5) / 1e5; l["lon"] = round(lo * 1e5) / 1e5; }
    }

    void posExtra(JsonObject o) {
        const RC::HeadingRotor& h = Drv::heading();
        if (!Cfg.headComp && !h.have()) return;
        const uint32_t now = millis();
        JsonObject d = o["hdg"].to<JsonObject>();
        d["on"] = Cfg.headComp;                                 // compensando (el acimut del evento es verdadero)
        if (!h.have()) return;
        d["v"] = round(h.heading() * 10) / 10;
        d["age"] = h.ageS(now);
        if (h.stale(now)) d["old"] = true;
    }
}

#else

namespace Lan {
    void begin() {}
    void loop() {}
    bool position(double& lat, double& lon) { lat = Cfg.lat; lon = Cfg.lon; return Cfg.lat != 0 || Cfg.lon != 0; }
    void info(JsonObject) {}
    void posExtra(JsonObject) {}
}

#endif
