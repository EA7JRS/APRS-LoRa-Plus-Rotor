/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "net.h"
#include "board.h"

#if !ROTOR_HAS_WIFI

namespace Net {
    void begin() {}
    void loop() {}
    std::string ip() { return ""; }
    bool up() { return false; }
}

#else

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#if ROTOR_ESP32
    #include <ESPmDNS.h>
#else
    #include <LEAmDNS.h>
#endif
#include "config.h"
#include "drivers.h"
#include "services.h"
#include "web_page.h"

namespace Net {

    static const int ROT_CLIENTS = 4, API_CLIENTS = 3;

    static WebServer        web(80);
    static HTTPUpdateServer updater;
    static WiFiServer*      rotSrv = nullptr;
    static WiFiServer*      apiSrv = nullptr;
    static WiFiClient       rotCli[ROT_CLIENTS];
    static RC::RotctlSession rotSess[ROT_CLIENTS];
    static WiFiClient       apiCli[API_CLIENTS];
    static ApiClient        apiSess[API_CLIENTS];
    static bool             started = false, apMode = false, mdnsOk = false;
    static uint32_t         staSince = 0;
    static std::string      webPass;

    bool up() { return apMode || WiFi.status() == WL_CONNECTED; }

    std::string ip() {
        if (apMode) return WiFi.softAPIP().toString().c_str();
        if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString().c_str();
        return "";
    }

    static bool authOk() {
        if (Cfg.pin.empty()) return true;
        if (web.authenticate(Cfg.webUser.c_str(), webPass.c_str())) return true;
        web.requestAuthentication(BASIC_AUTH, "APRS LoRa+ Rotor");
        return false;
    }

    static void handleApi() {
        if (!authOk()) return;
        const String body = web.arg("plain");
        RC::ApiSession s;
        s.authed = true;                       // ya autenticado por HTTP
        std::vector<std::string> out;
        std::string line;
        for (size_t i = 0; i <= body.length(); i++) {
            const char c = i < body.length() ? body[i] : '\n';
            if (c == '\n') { if (!line.empty()) s.handle(line, ApiHost, out); line.clear(); }
            else if (c != '\r' && line.size() < 1024) line += c;
        }
        std::string r = "[";
        for (size_t i = 0; i < out.size(); i++) { if (i) r += ','; r += out[i]; }
        r += "]";
        web.send(200, "application/json", r.c_str());
    }

    static void startServices() {
        if (started) return;
        started = true;
        webPass = Cfg.webPass.empty() ? Cfg.pin : Cfg.webPass;
        web.on("/", HTTP_GET, []() { if (authOk()) web.send(200, "text/html", WEB_PAGE); });
        web.on("/api", HTTP_POST, handleApi);
        web.onNotFound([]() { web.send(404, "text/plain", "not found"); });
        updater.setup(&web, "/update", Cfg.webUser.c_str(), webPass.c_str());
        web.begin();
        if (Cfg.rotctld) { rotSrv = new WiFiServer(Cfg.rotctldPort); rotSrv->begin(); rotSrv->setNoDelay(true); }
        apiSrv = new WiFiServer(Cfg.apiPort);
        apiSrv->begin();
        apiSrv->setNoDelay(true);
        mdnsOk = MDNS.begin(Cfg.name.c_str());
        if (mdnsOk) {
            MDNS.addService("http", "tcp", 80);
            if (Cfg.rotctld) MDNS.addService("rotctld", "tcp", Cfg.rotctldPort);
            MDNS.addService("aprsrotor", "tcp", Cfg.apiPort);
        }
        Board::startNtp(Cfg.ntp.c_str());
        logf("red lista: %s\n", ip().c_str());
    }

    static void startAp() {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(Cfg.name.c_str(), Cfg.apPass.c_str());
        apMode = true;
        logf("punto de acceso %s (%s)\n", Cfg.name.c_str(), WiFi.softAPIP().toString().c_str());
        startServices();
    }

    void begin() {
        if (Cfg.wifiMode == 0) { WiFi.mode(WIFI_OFF); return; }
        if (Cfg.wifiMode == 2 || Cfg.ssid.empty()) { startAp(); return; }
        WiFi.setHostname(Cfg.name.c_str());     // antes de WiFi.mode() en arduino-esp32 3.x
        WiFi.mode(WIFI_STA);
        WiFi.begin(Cfg.ssid.c_str(), Cfg.pass.c_str());
        staSince = millis();
    }

    template <int N, typename S>
    static void accept(WiFiServer* srv, WiFiClient (&cli)[N], S (&sess)[N]) {
        if (!srv) return;
        WiFiClient c = srv->accept();
        if (!c) return;
        for (int i = 0; i < N; i++) {
            if (!cli[i] || !cli[i].connected()) {
                cli[i].stop();
                cli[i] = c;
                cli[i].setNoDelay(true);
                sess[i].reset();
                return;
            }
        }
        c.stop();                              // lleno
    }

    void loop() {
        if (Cfg.wifiMode == 0) return;
        if (!apMode) {
            if (WiFi.status() == WL_CONNECTED) startServices();
            else if (!started && millis() - staSince > 30000) startAp();   // sin red: punto de acceso de rescate
        }
        if (!started) return;
        web.handleClient();
    #if ROTOR_RP2
        if (mdnsOk) MDNS.update();
    #endif
        RC::Rotor& rot = Drv::rotor();
        accept(rotSrv, rotCli, rotSess);
        accept(apiSrv, apiCli, apiSess);
        char buf[128];
        for (int i = 0; i < ROT_CLIENTS; i++) {
            WiFiClient& c = rotCli[i];
            if (!c || !c.connected()) continue;
            while (c.available()) {
                const int n = c.read((uint8_t*)buf, sizeof(buf));
                if (n <= 0) break;
                std::string out;
                const bool close = rotSess[i].feed(buf, n, rot, out);
                if (!out.empty()) c.write((const uint8_t*)out.data(), out.size());
                if (close) { c.stop(); break; }
            }
        }
        for (int i = 0; i < API_CLIENTS; i++) {
            WiFiClient& c = apiCli[i];
            if (!c || !c.connected()) continue;
            std::vector<std::string> out;
            while (c.available()) {
                const int n = c.read((uint8_t*)buf, sizeof(buf));
                if (n <= 0) break;
                apiSess[i].feed(buf, n, out);
            }
            apiSess[i].tick(out);
            for (auto& l : out) { c.write((const uint8_t*)l.data(), l.size()); c.write('\n'); }
        }
    }
}

#endif
