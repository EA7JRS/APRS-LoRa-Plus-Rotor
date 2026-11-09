/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later (ver LICENSE).
 */

#pragma once
// Piezas compartidas por los transportes: servidor de protocolo serie (USB / SPP) y anfitrion del protocolo JSON.
#include <Arduino.h>
#include <vector>
#include "rc_rotctld.h"
#include "rc_serialproto.h"
#include "rc_api.h"

// Puerto serie (USB, SPP) que habla uno de los protocolos de Hamlib: 0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog, 4 rotctld
class ProtoPort {
public:
    void setProto(int p) { proto_ = p; gs_.setModeB(p != 0); }
    void poll(Stream& s, RC::Rotor& rot);
private:
    void probe(Stream& s, const uint8_t* buf, int n);   // "APRS-INFO?" del instalador web: placa y version (en cualquier protocolo)
    int                  proto_ = 1;
    std::string          probe_;
    RC::GS232::Server    gs_;
    RC::EasyComm::Server ec_;
    RC::Rot2Prog::Server r2p_;
    RC::RotctlSession    rc_;
};

// Anfitrion del protocolo JSON (BLE, TCP 4534, WebUI)
class RotorApiHost : public RC::ApiHost {
public:
    RC::Rotor&  rotor() override;
    void        info(JsonObject o) override;
    void        posExtra(JsonObject o) override;
    std::string configJson() override;
    bool        applyConfig(const std::string& json, std::string& err) override;
    bool        calibrate(const char* axis, const char* pt, double deg, std::string& err) override;
    bool        setHere(const char* axis, double deg, std::string& err) override;
    void        raw(JsonObject o) override;
    void        clearFault() override;
    void        reboot() override;
    const char* pin() override;
    std::string lastAprs() override;
    RC::Rig*        rig() override;
    RC::CatTracker* catTracker() override;
    bool            catPttAllowed() override;
    uint32_t        nowMs() override { return millis(); }
    RC::WindGuard*  wind() override;
};
extern RotorApiHost ApiHost;

// Reinicio pedido (tras guardar la configuracion): se hace desde loop() para que salga antes la respuesta
void requestReboot(uint32_t afterMs = 800);
void serviceReboot();

// Sesion JSON con eventos de posicion periodicos (sub)
struct ApiClient {
    RC::ApiSession sess;
    std::string    in;
    uint32_t       lastEvt = 0, lastCat = 0;
    bool           wasMoving = false;
    // Devuelve las lineas a enviar por los datos recibidos y por el temporizador de eventos
    void feed(const char* data, size_t n, std::vector<std::string>& out);
    void tick(std::vector<std::string>& out);
    void reset() { sess = RC::ApiSession(); in.clear(); lastEvt = 0; lastCat = 0; wasMoving = false; }
};
