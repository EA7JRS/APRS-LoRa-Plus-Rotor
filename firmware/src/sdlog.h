/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Registro de movimientos en la microSD (LCD-7 / LCD-7B): un CSV por día (UTC) en /rotor/AAAA-MM-DD.csv con las órdenes
// (objetivo nuevo), el movimiento (cada log.everyS segundos), la llegada o la parada y los fallos. Sin hora válida,
// /rotor/nodate.csv con los segundos desde el arranque. Se descarga desde la WebUI (/sd).
#include <Arduino.h>
#include <ArduinoJson.h>
#if ROTOR_HAS_SD
    #include <FS.h>
#endif

namespace SdLog {
    void begin();
    void loop();
    void info(JsonObject o);            // estado para el evento info ("sd")
    bool ok();                          // tarjeta montada
#if ROTOR_HAS_SD
    void webList(String& html);         // lista de ficheros para la WebUI
    bool webOpen(const String& name, fs::File& f);   // nombre sin carpeta (AAAA-MM-DD.csv)
#endif
}
