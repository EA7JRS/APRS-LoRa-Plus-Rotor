/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Bluetooth: BLE con el protocolo JSON (app Android) o Bluetooth clasico SPP con un protocolo de Hamlib (tracker APRS LoRa+
// con GS-232). Uno de los dos (Cfg.btMode). BLE: ESP32 / ESP32-S3. SPP: ESP32 original y Pico 2 W.
#include <Arduino.h>

namespace Bt {
    void begin();
    void loop();
    bool connected();
}
