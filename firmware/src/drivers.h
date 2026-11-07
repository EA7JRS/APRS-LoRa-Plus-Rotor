/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Mando del rotor: control directo (reles / puente H + potenciometro por ADC o ADS1115) o pasarela serie a un controlador
// (GS-232A/B, EasyComm II, Rot2Prog). Todo lo demas (rotctld, BLE, USB, SPP, APRS) habla con Drv::rotor().
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include "rc_common.h"

namespace Drv {
    void        begin();
    void        loop();
    RC::Rotor&  rotor();
    const char* name();
    void        raw(JsonObject o);
    void        posExtra(JsonObject o);
    bool        calibrate(const char* axis, const char* pt, double deg, std::string& err);
    bool        setHere(const char* axis, double deg, std::string& err);
    void        clearFault();
    void        allOff();                       // salidas a reposo (fallo, reinicio)
    uint8_t     fault();
}
