/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// LoRa APRS: recibe mensajes para el rotor (docs/APRS_CONTROL.md), contesta y, si se pide, manda una baliza de estado.
// Emision sin bloquear: con SF12 una trama tarda segundos y el control del motor no puede esperar.
#include <Arduino.h>
#include <string>

namespace LoraAprs {
    void        begin();
    void        loop();
    bool        active();
    std::string lastResult();
    void        send(const std::string& tnc2);      // a la cola de emision
    float       lastRssi();
}
