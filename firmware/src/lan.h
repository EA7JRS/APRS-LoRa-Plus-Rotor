/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Red local: la misma conectividad que el plotter y APRS LoRa+ Display (lib/aprs_links y lib/plotter_core, copiadas del plotter con
// tools/sync_links.sh). Con la WiFi del rotor (cliente o su punto de acceso): un servidor TCP (NMEA 0183, gpsd o Signal K), escucha
// NMEA por UDP (el instrumento de viento de 4") y el tiempo de OpenWeatherMap. Se usa para:
//   - la proteccion por viento sin la app: viento aparente o real medido (MWV, Signal K) o el de OpenWeatherMap;
//   - la posicion de la estacion (baliza APRS y tiempo de internet) desde gpsd, NMEA o Signal K;
//   - el rumbo del barco o del coche para apuntar en acimut verdadero (lan.heading, rc_heading.h).
// Solo ESP32 / ESP32-S3 / ESP32-C5 (en la Pico no hace nada).
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Lan {
    void begin();
    void loop();
    bool position(double& lat, double& lon);    // de la red (lan.pos) o la de aprs.lat / lon; false si no hay ninguna
    void info(JsonObject o);                    // "lan" del evento info
    void posExtra(JsonObject o);                // "hdg" del evento pos
}
