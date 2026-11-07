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
 *
 * Controlador de rotores de antena compatible con Hamlib para el ecosistema APRS LoRa+ (tracker y app Android).
 */

#include <Arduino.h>
#include <LittleFS.h>
#include "board.h"
#include "config.h"
#include "drivers.h"
#include "services.h"
#include "net.h"
#include "bt.h"
#include "lora_aprs.h"

static ProtoPort usb;
static uint32_t  ledT = 0;

void setup() {
    Serial.begin(115200);
    Board::begin();
#if ROTOR_ESP32
    const bool fs = LittleFS.begin(true);
#else
    const bool fs = LittleFS.begin();
#endif
    if (!Config::load() && fs) Config::save();       // primer arranque: guarda los valores por defecto
    // Salidas en reposo lo antes posible
    Drv::begin();
    Drv::allOff();
    usb.setProto(Cfg.usbProto);
    if (Cfg.led >= 0) pinMode(Cfg.led, OUTPUT);
    logf("\nAPRS LoRa+ Rotor %s (%s, %s) - mando: %s\n", ROTOR_VERSION, ROTOR_BOARD, Board::chip(), Drv::name());
    Net::begin();
    Bt::begin();
    LoraAprs::begin();
}

void loop() {
    Drv::loop();
    if (Cfg.usbProto != 5) usb.poll(Serial, Drv::rotor());
    Net::loop();
    Bt::loop();
    LoraAprs::loop();
    serviceReboot();
    // LED: fijo moviendose, parpadeo rapido con fallo, destello lento en reposo
    if (Cfg.led >= 0) {
        const uint32_t now = millis();
        bool on;
        if (Drv::fault()) on = (now / 150) % 2;
        else if (Drv::rotor().status() & RC::ST_MOVING) on = true;
        else on = (now % 3000) < 60;
        if (now - ledT > 20) { ledT = now; digitalWrite(Cfg.led, on ? HIGH : LOW); }
    }
}
