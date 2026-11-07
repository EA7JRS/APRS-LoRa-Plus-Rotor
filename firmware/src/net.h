/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Red WiFi: servidor rotctld (Hamlib, puerto 4533), protocolo JSON por TCP (4534), WebUI (80) con actualizacion OTA y mDNS.
#include <Arduino.h>
#include <string>

namespace Net {
    void        begin();
    void        loop();
    std::string ip();
    bool        up();
}
