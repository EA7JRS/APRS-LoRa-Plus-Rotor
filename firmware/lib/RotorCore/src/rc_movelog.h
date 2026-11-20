/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Registro de movimientos: a partir de lo que se ve del rotor cada poco (posición, objetivo, si se mueve, fallo) saca las
// líneas del registro: MOVE (objetivo nuevo), MANUAL (se mueve sin objetivo), POS (cada everyS mientras se mueve),
// STOP (deja de moverse), FAULT / CLEAR (cambia el fallo), BOOT. Formato CSV:
//   utc,event,az,el,taz,tel,fault
// Sin Arduino: lo prueba test_host.
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>

namespace RC {

class MoveLog {
public:
    uint32_t everyMs = 5000;

    struct Snap { double az, el, taz, tel; bool moving; bool hasEl; std::string fault; };

    static const char* header() { return "utc,event,az,el,taz,tel,fault\n"; }

    // Devuelve las líneas nuevas (sin la marca de tiempo: la pone quien escribe, con stamp)
    void update(const Snap& s, uint32_t nowMs, const std::string& stamp, std::string& out) {
        if (!started_) { started_ = true; add(out, stamp, "BOOT", s); last_ = s; lastPosMs_ = nowMs; return; }
        if (s.fault != last_.fault) add(out, stamp, s.fault.empty() || s.fault == "OK" ? "CLEAR" : "FAULT", s);
        const bool newTarget = !isnan(s.taz) && (isnan(last_.taz) || fabs(s.taz - last_.taz) > 0.05 ||
                               (s.hasEl && (isnan(last_.tel) != isnan(s.tel) || (!isnan(s.tel) && fabs(s.tel - last_.tel) > 0.05))));
        if (newTarget) { add(out, stamp, "MOVE", s); lastPosMs_ = nowMs; }
        if (s.moving && !last_.moving && isnan(s.taz)) { add(out, stamp, "MANUAL", s); lastPosMs_ = nowMs; }
        if (s.moving && !newTarget && nowMs - lastPosMs_ >= everyMs) { add(out, stamp, "POS", s); lastPosMs_ = nowMs; }
        if (!s.moving && last_.moving) add(out, stamp, "STOP", s);
        last_ = s;
    }

private:
    bool started_ = false;
    Snap last_{0, 0, NAN, NAN, false, false, ""};
    uint32_t lastPosMs_ = 0;

    static void num(std::string& o, double v) {
        char b[16];
        if (isnan(v)) { o += ','; return; }
        snprintf(b, sizeof(b), ",%.1f", v);
        o += b;
    }
    static void add(std::string& o, const std::string& stamp, const char* ev, const Snap& s) {
        o += stamp; o += ','; o += ev;
        num(o, s.az); num(o, s.hasEl ? s.el : NAN); num(o, s.taz); num(o, s.hasEl ? s.tel : NAN);
        o += ','; o += (s.fault == "OK" ? "" : s.fault); o += '\n';
    }
};

}  // namespace RC
