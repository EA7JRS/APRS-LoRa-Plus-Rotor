/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 *
 * Bluetooth clasico SPP: la placa se anuncia con su nombre y habla un protocolo de Hamlib (GS-232B por defecto).
 * El tracker APRS LoRa+ (placas ESP32 originales, transporte "Bluetooth SPP" del rotor) se conecta por nombre y manda "Waaa eee".
 */

#include "board.h"
#if ROTOR_HAS_SPP

#if ROTOR_ESP32
    #include <BluetoothSerial.h>
    static BluetoothSerial SerialBT;
#else
    #include <SerialBT.h>
#endif
#include "config.h"
#include "drivers.h"
#include "services.h"

namespace BtSpp {

    static ProtoPort port;
    static bool      ok = false;

    void begin() {
        port.setProto(Cfg.sppProto);
    #if ROTOR_ESP32
        ok = SerialBT.begin(Cfg.name.c_str());
    #else
        SerialBT.setName(Cfg.name.c_str());
        SerialBT.begin();
        ok = true;
    #endif
        logf("SPP %s: %s\n", ok ? "listo" : "error", Cfg.name.c_str());
    }

    void loop() {
        if (!ok) return;
        port.poll(SerialBT, Drv::rotor());
    }

    bool connected() {
    #if ROTOR_ESP32
        return ok && SerialBT.hasClient();
    #else
        return ok && (bool)SerialBT;
    #endif
    }
}

#endif
