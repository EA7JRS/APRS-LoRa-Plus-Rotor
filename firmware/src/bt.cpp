/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#include "bt.h"
#include "board.h"
#include "config.h"

#if ROTOR_HAS_BLE
namespace BtBle { void begin(); void loop(); bool connected(); }
#endif
#if ROTOR_HAS_SPP
namespace BtSpp { void begin(); void loop(); bool connected(); }
#endif

namespace Bt {

    static int mode = 0;   // modo que de verdad se ha arrancado

    void begin() {
    #if ROTOR_HAS_BLE
        if (Cfg.btMode == 1) { BtBle::begin(); mode = 1; return; }
    #endif
    #if ROTOR_HAS_SPP
        if (Cfg.btMode == 2) { BtSpp::begin(); mode = 2; return; }
    #endif
        if (Cfg.btMode != 0) logf("Bluetooth: el modo %d no existe en esta placa\n", Cfg.btMode);
    }

    void loop() {
    #if ROTOR_HAS_BLE
        if (mode == 1) BtBle::loop();
    #endif
    #if ROTOR_HAS_SPP
        if (mode == 2) BtSpp::loop();
    #endif
    }

    bool connected() {
    #if ROTOR_HAS_BLE
        if (mode == 1) return BtBle::connected();
    #endif
    #if ROTOR_HAS_SPP
        if (mode == 2) return BtSpp::connected();
    #endif
        return false;
    }
}
