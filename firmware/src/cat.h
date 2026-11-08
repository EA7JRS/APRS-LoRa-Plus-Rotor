/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// CAT de la emisora (o del receptor SDR): enlace por UART (CI-V, Yaesu, Kenwood) o por WiFi (SDRconnect, rigctld), seguimiento con
// Doppler y paso de sintonía de la pantalla. Lo usan el servidor rigctld (net.cpp), la API JSON (services.cpp) y la pantalla.
#include <Arduino.h>
#include "rc_cat.h"

namespace Cat {
    void             begin();
    void             loop();
    RC::CatEngine*   engine();          // nullptr = sin CAT
    RC::CatTracker&  tracker();
    uint32_t         step();            // paso de sintonía (Hz) de la pantalla
    void             setStep(uint32_t hz);
    bool             tune(int steps);   // mueve la frecuencia N pasos (redondea al paso)
}
