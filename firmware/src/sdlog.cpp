/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "sdlog.h"
#include "config.h"
#include "board.h"
#include "drivers.h"
#include "services.h"
#include "rc_movelog.h"
#include <time.h>
#include <algorithm>
#include <vector>

#if ROTOR_HAS_SD
    #include <SD.h>
    #include <SPI.h>
    #include "exio.h"
    #ifndef SD_SCK
        #define SD_SCK 12
    #endif
    #ifndef SD_MISO
        #define SD_MISO 13
    #endif
    #ifndef SD_MOSI
        #define SD_MOSI 11
    #endif
    #ifndef SD_CS_EXIO
        #define SD_CS_EXIO 4                // CS de la tarjeta en el expansor (CH422G / CH32V003): bajo = elegida
    #endif
#endif

namespace SdLog {
#if ROTOR_HAS_SD
    static const char* DIR = "/rotor";
    static SPIClass*   spi = nullptr;
    static bool        mounted = false;
    static uint32_t    nextMount = 0, lastSnap = 0, lastFlush = 0, lastClean = 0;
    static RC::MoveLog mlog;
    static std::string buf;
    static String      curFile;
    static uint32_t    lines = 0, errors = 0;

    static bool timeOk() { return time(nullptr) > 1700000000; }

    static String dayFile(time_t t) {
        struct tm g;
        gmtime_r(&t, &g);
        char b[32];
        strftime(b, sizeof(b), "/rotor/%Y-%m-%d.csv", &g);
        return b;
    }

    static std::string stamp() {
        char b[32];
        if (timeOk()) {
            const time_t t = time(nullptr);
            struct tm g;
            gmtime_r(&t, &g);
            strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &g);
        } else {
            snprintf(b, sizeof(b), "+%lus", (unsigned long)(millis() / 1000));
        }
        return b;
    }

    static void mount() {
        if (!spi) {
            spi = new SPIClass(FSPI);
            spi->begin(SD_SCK, SD_MISO, SD_MOSI, Cfg.logCs);
        }
        Exio::output(SD_CS_EXIO);
        Exio::write(SD_CS_EXIO, false);                     // la única en el bus: se queda elegida
        mounted = SD.begin((uint8_t)Cfg.logCs, *spi, 20000000, "/sd", 5, false);
        if (!mounted) { SD.end(); return; }
        if (!SD.exists(DIR)) SD.mkdir(DIR);
        logf("sd: %s, %llu MB libres\n", SD.cardType() == CARD_SDHC ? "SDHC" : "SD",
             (unsigned long long)((SD.totalBytes() - SD.usedBytes()) >> 20));
    }

    // Borra los días de hace más de log.keepDays (nombres AAAA-MM-DD.csv: se comparan como texto)
    static void clean() {
        if (!Cfg.logKeepDays || !timeOk()) return;
        const String cut = dayFile(time(nullptr) - (time_t)Cfg.logKeepDays * 86400).substring(7);   // sin "/rotor/"
        File d = SD.open(DIR);
        if (!d) return;
        std::vector<String> old;
        for (File f = d.openNextFile(); f; f = d.openNextFile()) {
            const String n = f.name();
            if (n.length() == 14 && n.endsWith(".csv") && n < cut) old.push_back(n);
            f.close();
        }
        d.close();
        for (const String& n : old) SD.remove(String(DIR) + "/" + n);
        if (!old.empty()) logf("sd: %u días antiguos borrados\n", (unsigned)old.size());
    }

    static void flush() {
        if (buf.empty() || !mounted) return;
        const String path = timeOk() ? dayFile(time(nullptr)) : String("/rotor/nodate.csv");
        const bool fresh = !SD.exists(path);
        File f = SD.open(path, FILE_APPEND);
        if (!f) { mounted = false; errors++; SD.end(); nextMount = millis() + 30000; return; }   // tarjeta quitada
        if (fresh) f.print(RC::MoveLog::header());
        const size_t n = f.write((const uint8_t*)buf.data(), buf.size());
        f.close();
        if (n != buf.size()) { errors++; return; }
        curFile = path;
        buf.clear();
    }

    void begin() {
        if (!Cfg.logSd) return;
        mlog.everyMs = (uint32_t)Cfg.logEveryS * 1000u;
        mount();
        if (!mounted) nextMount = millis() + 30000;
        clean();
        lastClean = millis();
    }

    void loop() {
        if (!Cfg.logSd) return;
        const uint32_t now = millis();
        if (!mounted && (int32_t)(now - nextMount) >= 0) {
            mount();
            nextMount = now + 30000;
        }
        if (now - lastSnap >= 250) {
            lastSnap = now;
            RC::Rotor& r = Drv::rotor();
            RC::MoveLog::Snap s;
            r.getPos(s.az, s.el);
            JsonDocument d;
            JsonObject o = d.to<JsonObject>();
            Drv::posExtra(o);
            s.taz = o["taz"].is<double>() ? o["taz"].as<double>() : NAN;
            s.tel = o["tel"].is<double>() ? o["tel"].as<double>() : NAN;
            s.moving = (r.status() & RC::ST_MOVING) != 0;
            s.hasEl = r.hasEl();
            s.fault = RC::faultText(Drv::fault());
            const size_t before = buf.size();
            mlog.update(s, now, stamp(), buf);
            for (size_t i = before; i < buf.size(); i++) if (buf[i] == '\n') lines++;
            if (buf.size() > 8192) buf.erase(0, buf.size() - 8192);     // sin tarjeta: se queda lo último
        }
        if (mounted && !buf.empty() && (now - lastFlush >= 10000 || buf.size() > 2048)) {
            lastFlush = now;
            flush();
        }
        if (mounted && now - lastClean >= 6UL * 3600 * 1000) { lastClean = now; clean(); }
    }

    bool ok() { return mounted; }

    void info(JsonObject o) {
        o["on"] = Cfg.logSd;
        o["ok"] = mounted;
        if (!mounted) return;
        o["file"] = curFile.c_str();
        o["freeMB"] = (uint32_t)((SD.totalBytes() - SD.usedBytes()) >> 20);
        o["lines"] = lines;
        if (errors) o["err"] = errors;
    }

    void webList(String& h) {
        flush();
        h = "<!DOCTYPE html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
            "<title>APRS LoRa+ Rotor · microSD</title><body style='font-family:sans-serif;background:#131A22;color:#ddd'>"
            "<h3>Registro de movimientos (microSD)</h3>";
        if (!mounted) { h += "<p>Sin tarjeta.</p>"; return; }
        File d = SD.open(DIR);
        std::vector<String> names;
        if (d) {
            for (File f = d.openNextFile(); f; f = d.openNextFile()) {
                const String n = f.name();
                if (n.endsWith(".csv")) names.push_back(n + "|" + String((unsigned long)f.size()));
                f.close();
            }
            d.close();
        }
        std::sort(names.begin(), names.end(), [](const String& a, const String& b) { return a > b; });
        h += "<ul>";
        for (const String& s : names) {
            const int p = s.indexOf('|');
            const String n = s.substring(0, p);
            h += String("<li><a style='color:#3CE68A' href='/sd?f=") + n + "'>" + n + "</a> (" + s.substring(p + 1) + " bytes)</li>";
        }
        h += String("</ul><p>") + String((unsigned long)((SD.totalBytes() - SD.usedBytes()) >> 20)) + " MB libres</p>";
    }

    bool webOpen(const String& name, fs::File& f) {
        if (!mounted || name.indexOf('/') >= 0 || name.indexOf("..") >= 0 || !name.endsWith(".csv")) return false;
        flush();
        f = SD.open(String(DIR) + "/" + name, FILE_READ);
        return (bool)f;
    }
#else
    void begin() {}
    void loop() {}
    bool ok() { return false; }
    void info(JsonObject o) { o["on"] = false; }
#endif
}
