/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later.
 *
 * Pruebas del nucleo en el PC (sin placa):  make -C esp32/test_host
 */
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "rc_rotctld.h"
#include "rc_serialproto.h"
#include "rc_control.h"
#include "rc_aprs.h"
#include "rc_api.h"
#include "rc_cat.h"
#include "rc_rigctld.h"
#include "rc_wind.h"
#include "rc_heading.h"
#include "rc_anemo.h"

using namespace RC;

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) CHECK(fabs((a) - (b)) <= (tol))

// Rotor de pruebas: guarda lo pedido
struct FakeRotor : Rotor {
    double az = 10, el = 20, minA = 0, maxA = 450, maxE = 180;
    int lastMove = 0, lastSpeed = -1;
    bool stopped = false, parked = false, el2 = true;
    bool setPos(double a, double e) override { az = a; el = e; return true; }
    bool setAz(double a) override { az = a; return true; }
    void getPos(double& a, double& e) override { a = az; e = el; }
    bool stop() override { stopped = true; return true; }
    bool park() override { parked = true; return true; }
    bool move(int d, int s) override { lastMove = d; lastSpeed = s; return true; }
    uint32_t status() override { return ST_MOVING | ST_MOVING_AZ; }
    const char* info() override { return "fake"; }
    double minAz() const override { return minA; }
    double maxAz() const override { return maxA; }
    double maxEl() const override { return maxE; }
    bool hasEl() const override { return el2; }
};

static std::string run(RotctlSession& s, Rotor& r, const char* txt, bool* closed = nullptr) {
    std::string out;
    bool c = s.feed(txt, strlen(txt), r, out);
    if (closed) *closed = c;
    return out;
}

static void testRotctld() {
    FakeRotor r;
    RotctlSession s;
    CHECK(run(s, r, "\\dump_state\n") == "1\n2\nmin_az=0.000000\nmax_az=450.000000\nmin_el=0.000000\nmax_el=180.000000\nsouth_zero=0\nrot_type=AzEl\ndone\n");
    CHECK(run(s, r, "P 123.5 45\n") == "RPRT 0\n");
    NEAR(r.az, 123.5, 1e-9); NEAR(r.el, 45, 1e-9);
    CHECK(run(s, r, "p\n") == "123.500000\n45.000000\n");
    CHECK(run(s, r, "\\set_pos 400 10\n") == "RPRT 0\n");
    NEAR(r.az, 400, 1e-9);                                  // dentro del rango mecanico: se respeta
    CHECK(run(s, r, "P -10 5\n") == "RPRT 0\n");
    NEAR(r.az, 350, 1e-9);                                  // fuera del rango: 0-360
    CHECK(run(s, r, "P abc 5\n") == "RPRT -1\n");
    CHECK(run(s, r, "P 10\n") == "RPRT -1\n");
    CHECK(run(s, r, "M 16 50\n") == "RPRT 0\n");
    CHECK(r.lastMove == MOVE_RIGHT && r.lastSpeed == 50);
    CHECK(run(s, r, "\\move CCW -1\n") == "RPRT 0\n");
    CHECK(r.lastMove == MOVE_LEFT && r.lastSpeed == 0);
    CHECK(run(s, r, "M 3 50\n") == "RPRT -1\n");
    CHECK(run(s, r, "S\n") == "RPRT 0\n" && r.stopped);
    CHECK(run(s, r, "K\n") == "RPRT 0\n" && r.parked);
    CHECK(run(s, r, "s\n") == "MOVING MOVING_AZ \n");
    CHECK(run(s, r, "_\n") == "fake\n");
    CHECK(run(s, r, "Z\n") == "RPRT -4\n");
    CHECK(run(s, r, "+p\n") == "RPRT -4\n");
    // Lineas partidas en varios paquetes TCP y CRLF
    std::string o;
    s.feed("P 1", 3, r, o); s.feed("0 20\r", 5, r, o); s.feed("\n", 1, r, o);
    CHECK(o == "RPRT 0\n"); NEAR(r.az, 10, 1e-9);
    bool closed = false;
    CHECK(run(s, r, "q\n", &closed) == "RPRT 0\n" && closed);
    r.el2 = false;
    CHECK(run(s, r, "\\dump_state\n").find("rot_type=Az\n") != std::string::npos);
}

static void testGs232() {
    FakeRotor r;
    GS232::Server sb(true), sa(false);
    std::string o;
    r.az = 123; r.el = 45;
    sb.feed("C2\r", 3, r, o);   CHECK(o == "AZ=123  EL=045\r\n"); o.clear();
    sb.feed("c\r", 2, r, o);    CHECK(o == "AZ=123\r\n"); o.clear();
    sb.feed("B\r", 2, r, o);    CHECK(o == "EL=045\r\n"); o.clear();
    sa.feed("C2\r", 3, r, o);   CHECK(o == "+0123+0045\r\n"); o.clear();
    sb.feed("W210 030\r", 9, r, o); CHECK(o.empty()); NEAR(r.az, 210, 0); NEAR(r.el, 30, 0);
    sb.feed("M090\r", 5, r, o); CHECK(o.empty()); NEAR(r.az, 90, 0);
    sb.feed("W999 030\r", 9, r, o); CHECK(o == "?>\r\n"); o.clear();
    sb.feed("X2\rR\r", 5, r, o); CHECK(r.lastMove == MOVE_RIGHT && r.lastSpeed == 50);
    sb.feed("S\r", 2, r, o); CHECK(r.stopped);
    sb.feed("Q\r", 2, r, o); CHECK(o == "?>\r\n"); o.clear();
    // Lo que manda el tracker APRS LoRa+ por SPP
    sb.feed("W123 045\r", 9, r, o); NEAR(r.az, 123, 0); NEAR(r.el, 45, 0);
    // Cliente
    CHECK(GS232::cmdSet(5.4, 30.6, true) == "W005 031\r");
    CHECK(GS232::cmdSet(-10, 0, false) == "M350\r");
    double a, e; bool he;
    CHECK(GS232::parsePos("AZ=123  EL=045\r\n", a, e, he) && he); NEAR(a, 123, 0); NEAR(e, 45, 0);
    CHECK(GS232::parsePos("+0123+0045", a, e, he) && he);       NEAR(a, 123, 0); NEAR(e, 45, 0);
    CHECK(GS232::parsePos("AZ=300", a, e, he) && !he);          NEAR(a, 300, 0);
    CHECK(!GS232::parsePos("?>", a, e, he));
    CHECK(GS232::cmdSpeed(60) == "X3\r");
}

static void testEasyComm() {
    FakeRotor r;
    EasyComm::Server s;
    std::string o;
    s.feed("AZ123.4 EL45.6 UP000 XXX DN000 XXX\n", 35, r, o); CHECK(o.empty()); NEAR(r.az, 123.4, 1e-6); NEAR(r.el, 45.6, 1e-6);
    s.feed("AZ\n", 3, r, o); CHECK(o == "AZ123.4\n"); o.clear();
    s.feed("EL\n", 3, r, o); CHECK(o == "EL45.6\n"); o.clear();
    s.feed("AZ EL\n", 6, r, o); CHECK(o == "AZ123.4 EL45.6\n"); o.clear();
    s.feed("SA SE \n", 7, r, o); CHECK(r.stopped);
    s.feed("MR\n", 3, r, o); CHECK(r.lastMove == MOVE_RIGHT);
    CHECK(EasyComm::cmdSet(370, 10) == "AZ10.0 EL10.0\n");
    double a = 0, e = 0; bool ga, ge;
    CHECK(EasyComm::parseReply("AZ200.5", a, e, ga, ge) && ga && !ge); NEAR(a, 200.5, 1e-6);
    CHECK(EasyComm::parseReply("AZ1.0 EL2.5\n", a, e, ga, ge) && ga && ge); NEAR(e, 2.5, 1e-6);
}

static void testRot2Prog() {
    uint8_t f[13];
    Rot2Prog::frameSet(f, 123.4, 45.0, 10, 10);
    // 10 * (360 + 123.4) = 4834 ; 10 * 405 = 4050
    CHECK(memcmp(f, "\x57" "4834" "\x0a" "4050" "\x0a" "\x2f" "\x20", 13) == 0);
    uint8_t r[12];
    Rot2Prog::reply(r, 123.4, 45.0);
    double a, e;
    CHECK(Rot2Prog::parseReply(r, a, e)); NEAR(a, 123.4, 1e-6); NEAR(e, 45.0, 1e-6);
    // Servidor
    FakeRotor rot;
    Rot2Prog::Server s;
    std::string o;
    uint8_t junk[2] = { 0x00, 0x13 };
    s.feed(junk, 2, rot, o);
    s.feed(f, 13, rot, o);
    NEAR(rot.az, 123.4, 1e-6); NEAR(rot.el, 45.0, 1e-6);
    CHECK(o.size() == 12);
    CHECK(Rot2Prog::parseReply((const uint8_t*)o.data(), a, e)); NEAR(a, 123.4, 1e-6);
    o.clear();
    Rot2Prog::frameSimple(f, Rot2Prog::K_STOP);
    s.feed(f, 13, rot, o); CHECK(rot.stopped && o.size() == 12);
    // Valor tipico de hamlib spid.c con PH = 1 (Rot1Prog / resolucion 1 grado)
    Rot2Prog::frameSet(f, 90, 0, 1, 1);
    o.clear(); s.feed(f, 13, rot, o); NEAR(rot.az, 90, 1e-6);
}

// Motor simulado: grados por segundo a velocidad 1; raw = lectura del ADC
struct SimAxis {
    double pos, degPerS;
    double rawPerDeg = 4095.0 / 450.0;
    double raw() const { return pos * rawPerDeg; }
    void step(const AxisOut& o, double dt, bool jam = false) { if (!jam && o.dir) pos += o.dir * o.speed * degPerS * dt; }
};

static void testControl() {
    DirectRotor rot;
    rot.az.cfg.minDeg = 0; rot.az.cfg.maxDeg = 450;
    rot.az.cfg.rawA = 0; rot.az.cfg.degA = 0; rot.az.cfg.rawB = 4095; rot.az.cfg.degB = 450;
    rot.az.cfg.filter = 1.0;
    rot.el.cfg.enabled = true; rot.el.cfg.minDeg = 0; rot.el.cfg.maxDeg = 180;
    rot.el.cfg.rawA = 0; rot.el.cfg.degA = 0; rot.el.cfg.rawB = 4095; rot.el.cfg.degB = 180; rot.el.cfg.filter = 1.0;
    SimAxis sa{ 400, 6 }, se{ 10, 3 };
    se.rawPerDeg = 4095.0 / 180.0;
    uint32_t t = 1000;
    auto sim = [&](double secs, bool jam = false) {
        for (int i = 0; i < secs * 50; i++) {
            t += 20;
            rot.update(t, sa.raw(), se.raw());
            sa.step(rot.az.out(), 0.02, jam);
            se.step(rot.el.out(), 0.02, jam);
        }
    };
    sim(0.2);
    double a, e;
    rot.getPos(a, e); NEAR(a, 40, 0.1); NEAR(e, 10, 0.1);
    CHECK(rot.status() & ST_OVERLAP_RIGHT);
    // 30 grados: el mecanico mas cercano a 400 es 390 (no 30)
    NEAR(rot.mechFor(30), 390, 1e-9);
    rot.setPos(30, 45);
    sim(30);
    NEAR(sa.pos, 390, 1.5); NEAR(se.pos, 45, 1.5);
    CHECK(!(rot.status() & ST_MOVING));
    // Sin oscilaciones: pequeños cambios dentro de "restart" no lo mueven
    const double before = sa.pos;
    rot.setPos(30.3, 45);
    sim(2);
    NEAR(sa.pos, before, 0.6);
    // Invertir: pausa antes de cambiar de sentido
    rot.setPos(200, 45);
    sim(0.5);
    CHECK(rot.az.moving() == -1);
    rot.setPos(50, 45);              // mecanico 410: hacia arriba
    t += 20; rot.update(t, sa.raw(), se.raw());
    CHECK(rot.az.moving() == 0);
    sim(0.6);
    CHECK(rot.az.moving() == 0);      // aun en la pausa de 1,2 s
    sim(1.0);
    CHECK(rot.az.moving() == 1);
    sim(40);
    NEAR(sa.pos, 410, 1.5);
    // Manual hasta el tope
    rot.move(MOVE_RIGHT, 100);
    sim(20);
    NEAR(sa.pos, 449.5, 1.5);
    CHECK(rot.az.moving() == 0);
    CHECK(rot.status() & ST_LIMIT_RIGHT);
    // Atasco
    rot.setPos(100, 45);
    sim(8, true);
    CHECK(rot.fault() == F_STALL_AZ);
    CHECK(rot.az.moving() == 0);
    rot.setPos(100, 45);             // una orden nueva borra el fallo
    CHECK(rot.fault() == F_NONE);
    sim(80);
    NEAR(sa.pos, 100, 1.5);
    // Sensor sin dato
    t += 20; rot.update(t, NAN, se.raw());
    CHECK(rot.fault() == F_SENSOR);
    // Rango con tope al sur (-180..180)
    DirectRotor s2;
    s2.az.cfg.minDeg = -180; s2.az.cfg.maxDeg = 180;
    s2.az.cfg.rawA = 0; s2.az.cfg.degA = -180; s2.az.cfg.rawB = 4095; s2.az.cfg.degB = 180;
    s2.update(0, 4095.0 * 170 / 360, 0);      // -10 grados
    NEAR(s2.mechFor(350), -10, 1e-9);
    NEAR(s2.mechFor(10), 10, 1e-9);
    NEAR(s2.mechFor(180), -180, 1e-9);     // 180 y -180 valen: el mas cercano a -10
    // Rango de 270 grados (no cabe todo): va al tope mas cercano
    DirectRotor s3;
    s3.az.cfg.minDeg = 0; s3.az.cfg.maxDeg = 270;
    NEAR(s3.mechFor(300), 270, 1e-9);
    NEAR(s3.mechFor(350), 0, 1e-9);
    // Freno: se suelta antes de mover y se echa despues
    Axis b;
    b.cfg.minDeg = 0; b.cfg.maxDeg = 360; b.cfg.rawB = 360; b.cfg.degB = 360; b.cfg.brakeMs = 500; b.cfg.filter = 1;
    b.cfg.rampMs = 0; b.cfg.stallMs = 0;
    uint32_t tb = 100000;
    b.update(tb, 10);
    b.setTarget(50);
    AxisOut o = b.update(tb += 20, 10);
    CHECK(o.dir == 0 && o.brakeRelease);
    o = b.update(tb += 300, 10);  CHECK(o.dir == 0 && o.brakeRelease);
    o = b.update(tb += 300, 10);  CHECK(o.dir == 1 && o.brakeRelease);
    o = b.update(tb += 20, 50);   CHECK(o.dir == 0 && o.brakeRelease);
    o = b.update(tb += 600, 50);  CHECK(o.dir == 0 && !o.brakeRelease);
}

static void testSensorless() {
    Axis a;
    a.cfg.sensorless = true; a.cfg.rampMs = 0; a.cfg.manualMaxMs = 1000;
    uint32_t t = 5000;
    AxisOut o = a.update(t, NAN);
    CHECK(a.fault() == F_NONE && o.dir == 0);
    a.setTarget(100);                       // sin sensor: ignorado
    o = a.update(t += 20, NAN); CHECK(o.dir == 0);
    a.setManual(1, 0);
    o = a.update(t += 100, NAN); CHECK(o.dir == 1);
    o = a.update(t += 500, NAN); CHECK(o.dir == 1);
    o = a.update(t += 600, NAN); CHECK(o.dir == 0);   // tiempo maximo
}

static void testTotp() {
    // RFC 6238, anexo B (SHA1, clave "12345678901234567890")
    uint8_t key[32];
    const int kl = Crypto::base32("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", key, sizeof(key));
    CHECK(kl == 20 && memcmp(key, "12345678901234567890", 20) == 0);
    CHECK(Crypto::hotp(key, 20, 59 / 30, 8) == 94287082u);
    CHECK(Crypto::hotp(key, 20, 1111111109ULL / 30, 8) == 7081804u);
    CHECK(Crypto::hotp(key, 20, 1234567890ULL / 30, 8) == 89005924u);
    CHECK(Crypto::hotp(key, 20, 2000000000ULL / 30, 8) == 69279037u);
    // RFC 4226 (HOTP), contador 0 y 9
    CHECK(Crypto::hotp((const uint8_t*)"12345678901234567890", 20, 0) == 755224u);
    CHECK(Crypto::hotp((const uint8_t*)"12345678901234567890", 20, 9) == 520489u);
}

static void testAprs() {
    Aprs::Message m;
    CHECK(Aprs::parseMessage(std::string("<\xff\x01", 3) + "EA7JRS-9>APLRT1,WIDE1-1::EA7JRS-12:P 123 45{17", m));
    CHECK(m.src == "EA7JRS-9" && m.dest == "APLRT1" && m.path == "WIDE1-1" && m.addressee == "EA7JRS-12" && m.text == "P 123 45" && m.id == "17");
    CHECK(Aprs::parseMessage("EA7JRS-9>APLRT1::EA7JRS-12:hola{AB}CD", m) && m.id == "AB" && m.text == "hola");
    CHECK(Aprs::parseMessage("EA1AAA>APRS,TCPIP*,qAC,T2:}EA7JRS-9>APLRT1,TCPIP,EA1AAA*::EA7JRS-12:?", m) && m.src == "EA7JRS-9" && m.text == "?");
    CHECK(!Aprs::parseMessage("EA7JRS-9>APLRT1:!3700.00N/00600.00W>", m));
    CHECK(Aprs::buildMessage("EA7JRS-12", "APZLRR", "", "EA7JRS-9", "ack17") == "EA7JRS-12>APZLRR::EA7JRS-9 :ack17");

    FakeRotor r;
    AprsControl c;
    c.cfg.callsign = "EA7JRS-12";
    std::string tx[4];
    size_t n;
    CHECK(c.allowed("EA7JRS-9") && c.allowed("ea7jrs") && !c.allowed("EA1AAA"));
    CHECK(c.onFrame("EA7JRS-9>APLRT1::EA7JRS-12:P 123 45{17", 1000, 0, r, tx, 4, n));
    CHECK(n == 2 && tx[0] == "EA7JRS-12>APZLRR::EA7JRS-9 :ack17" && tx[1].find("GO AZ 123 EL 45") != std::string::npos);
    NEAR(r.az, 123, 0); NEAR(r.el, 45, 0);
    // Repetido: solo el ack, no se ejecuta otra vez
    r.az = 0;
    CHECK(c.onFrame("EA7JRS-9>APLRT1::EA7JRS-12:P 123 45{17", 5000, 0, r, tx, 4, n) && n == 1 && tx[0].find("ack17") != std::string::npos);
    NEAR(r.az, 0, 0);
    // Sin numero: sin ack
    CHECK(c.onFrame("EA7JRS-9>APLRT1::EA7JRS-12:az 200 el 10", 6000, 0, r, tx, 4, n) && n == 1);
    NEAR(r.az, 200, 0); NEAR(r.el, 10, 0);
    CHECK(c.onFrame("EA7JRS-9>APLRT1::EA7JRS-12:P90", 6100, 0, r, tx, 4, n)); NEAR(r.az, 90, 0);
    CHECK(c.onFrame("EA7JRS-9>APLRT1::EA7JRS-12:PARK{2", 7000, 0, r, tx, 4, n) && r.parked);
    CHECK(c.onFrame("EA7JRS-9>APLRT1::EA7JRS-12:?{3", 8000, 0, r, tx, 4, n) && n == 2 && tx[1].find("AZ 90 EL 10") != std::string::npos);
    // No permitido -> rej
    CHECK(c.onFrame("EA1AAA>APLRT1::EA7JRS-12:P 1 1{5", 9000, 0, r, tx, 4, n) && n == 1 && tx[0].find(":rej5") != std::string::npos);
    // Otro destinatario
    CHECK(!c.onFrame("EA7JRS-9>APLRT1::EA7JRS-11:P 1 1{5", 9000, 0, r, tx, 4, n));
    // TOTP
    c.cfg.totpSecret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    const uint64_t now = 1762500000ULL;
    char code[8];
    snprintf(code, sizeof(code), "%06u", Crypto::hotp((const uint8_t*)"12345678901234567890", 20, now / 30));
    std::string f = std::string("EA7JRS-9>APLRT1::EA7JRS-12:P 10 20 ") + code + "{9";
    CHECK(c.onFrame(f, 10000, now, r, tx, 4, n) && n == 2 && tx[0].find("ack9") != std::string::npos); NEAR(r.az, 10, 0);
    // El mismo codigo no vale dos veces (aunque cambie el numero de mensaje)
    r.az = 0;
    f = std::string("EA7JRS-9>APLRT1::EA7JRS-12:P 10 20 ") + code + "{10";
    CHECK(c.onFrame(f, 11000, now, r, tx, 4, n) && tx[0].find("rej10") != std::string::npos); NEAR(r.az, 0, 0);
    CHECK(c.onFrame("EA7JRS-9>APLRT1::EA7JRS-12:P 10 20 123456{11", 12000, now, r, tx, 4, n) && tx[0].find("rej11") != std::string::npos);
    CHECK(c.onFrame("EA7JRS-9>APLRT1::EA7JRS-12:P 10 20 123456{12", 12000, 0, r, tx, 4, n) && tx[1] .find("ERR no time") != std::string::npos);
}

struct FakeHost : ApiHost {
    FakeRotor r;
    std::string cfg = "{\"wifi\":{\"ssid\":\"casa\"},\"name\":\"" + std::string(500, 'x') + "\"}";
    std::string applied;
    bool rebooted = false;
    Rotor& rotor() override { return r; }
    void info(JsonObject o) override { o["dev"] = "APRS LoRa+ Rotor"; o["ver"] = "5.4.2"; }
    std::string configJson() override { return cfg; }
    bool applyConfig(const std::string& j, std::string& e) override { JsonDocument d; if (deserializeJson(d, j)) { e = "json"; return false; } applied = j; return true; }
    bool calibrate(const char*, const char*, double, std::string&) override { return true; }
    bool setHere(const char*, double, std::string&) override { return true; }
    void raw(JsonObject o) override { o["az"] = 1234; }
    void clearFault() override {}
    void reboot() override { rebooted = true; }
    const char* pin() override { return "123456"; }
};

// Host con proteccion por viento: el rotor de verdad va detras de WindRotor, como en el firmware
struct WindHost : FakeHost {
    WindGuard g;
    WindRotor w{g};
    uint32_t t = 1000;
    WindHost() { w.attach(&r); }
    Rotor& rotor() override { return w; }
    WindGuard* wind() override { return &g; }
    uint32_t nowMs() override { return t; }
    const char* pin() override { return ""; }
    void clearFault() override { g.release(); }
};

static void testWind() {
    // Guarda sola
    WindGuard g;
    g.cfg.on = true; g.cfg.parkMs = 15; g.cfg.resumeMs = 10; g.cfg.holdMin = 15;
    CHECK(!g.update(8, 12, 270, 60, "OWM", 0) && !g.locked());
    CHECK(g.update(9, 16.5, 270, 60, "OWM", 1000) && g.locked());          // racha de 16,5: bloquea (true solo la primera vez)
    CHECK(!g.update(9, 17, 270, 60, "OWM", 2000) && g.locked());
    NEAR(g.peak(), 17, 1e-9);
    CHECK(!g.update(5, 8, 270, 0, "OWM", 2000 + 5 * 60000) && g.locked()); // calma, pero aun no pasan 15 min
    CHECK(g.holdLeftS(2000 + 5 * 60000) == 600);
    g.tick(2000 + 15 * 60000); CHECK(!g.locked());                          // 15 min sin rachas fuertes: libre
    CHECK(g.update(16, -1, -1, 0, "", 2000 + 16 * 60000) && g.locked());    // sin racha, cuenta el viento medio
    CHECK(!g.update(12, 13, -1, 0, "", 2000 + 40 * 60000) && g.locked());   // entre resume y park: sigue aparcado
    g.release(); CHECK(!g.locked());                                         // a mano
    CHECK(g.update(15, 15, -1, 0, "", 2000 + 41 * 60000));                   // la siguiente racha fuerte vuelve a bloquear
    g.cfg.on = false; g.tick(2000 + 42 * 60000); CHECK(!g.locked());
    CHECK(!g.update(30, 40, -1, 0, "", 2000 + 43 * 60000) && !g.locked());  // apagada: nunca bloquea
    CHECK(g.ageS(2000 + 43 * 60000 + 5000) == 5);

    // Por la API: aparca, rechaza movimientos, deja parar, "clear" libera
    WindHost h;
    h.g.cfg.on = true;
    ApiSession s;
    std::vector<std::string> o;
    s.handle("{\"c\":\"info\"}", h, o); CHECK(o[0].find("\"wind\":true") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"wx\",\"ws\":20,\"wg\":28.4,\"wd\":250,\"age\":120,\"src\":\"OWM\"}", h, o);
    CHECK(o[0] == "{\"e\":\"ok\",\"c\":\"wx\"}" && h.r.parked && h.r.stopped && h.g.locked());
    o.clear(); s.handle("{\"c\":\"pos\",\"az\":100}", h, o); CHECK(o[0].find("rejected") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"move\",\"d\":\"cw\"}", h, o); CHECK(o[0].find("rejected") != std::string::npos);
    CHECK(!h.w.setAz(5) && h.w.stop() && h.w.park());
    h.t += 30000;
    o.clear(); s.handle("{\"c\":\"get\"}", h, o);
    CHECK(o[0].find("\"wind\":{\"on\":true,\"park\":15,\"lk\":true,\"ws\":20,\"wg\":28.4,\"wd\":250,\"pk\":28.4,\"age\":150,\"src\":\"OWM\",\"hold\":870}") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"clear\"}", h, o); CHECK(!h.g.locked());
    o.clear(); s.handle("{\"c\":\"pos\",\"az\":100}", h, o); CHECK(o[0] == "{\"e\":\"ok\",\"c\":\"pos\"}");
    o.clear(); s.handle("{\"c\":\"wx\"}", h, o); CHECK(o[0].find("err") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"wx\",\"ws\":-3}", h, o); CHECK(o[0].find("range") != std::string::npos);
    // Sin proteccion: "wx" no se conoce y "get" no lleva "wind"
    FakeHost f; ApiSession s2;
    o.clear(); s2.handle("{\"c\":\"auth\",\"pin\":\"123456\"}", f, o);
    o.clear(); s2.handle("{\"c\":\"wx\",\"ws\":3}", f, o); CHECK(o[0].find("no wind") != std::string::npos);
    o.clear(); s2.handle("{\"c\":\"get\"}", f, o); CHECK(o[0].find("wind") == std::string::npos);
    CHECK(std::string(faultText(F_WIND)) == "WIND");
    // Con anemómetro en la caja, el tiempo de internet no cuenta
    WindHost h2; h2.g.cfg.on = true; h2.g.local = true; ApiSession s3;
    o.clear(); s3.handle("{\"c\":\"wx\",\"ws\":30,\"wg\":40}", h2, o);
    CHECK(o[0] == "{\"e\":\"ok\",\"c\":\"wx\"}" && !h2.g.locked() && !h2.g.have());
    h2.g.update(4, 6, -1, 0, "anemo", h2.t);
    o.clear(); s3.handle("{\"c\":\"get\"}", h2, o); CHECK(o[0].find("\"src\":\"anemo\",\"local\":true") != std::string::npos);
}

// Rotor en un barco: acimut verdadero <-> respecto a la proa, y volver a apuntar al girar
static void testHeading() {
    FakeRotor f; f.az = 0; f.maxA = 450;
    HeadingRotor h; h.attach(&f);
    double a, e;
    CHECK(h.setPos(100, 30) && f.az == 100);                 // apagado: tal cual
    h.enabled = true;
    CHECK(h.setPos(120, 30) && f.az == 120 && !h.active());  // sin rumbo aun: tal cual
    h.setHeading(90, 1000);
    CHECK(h.active() && h.minAz() == 0 && h.maxAz() == 360);
    h.getPos(a, e); NEAR(a, 210, 1e-9);                       // rotor a 120 de la proa con proa 090 = 210 verdadero
    CHECK(h.setPos(45, 10)); NEAR(f.az, 315, 1e-9); NEAR(f.el, 10, 1e-9);   // 45 - 90 = -45 -> 315
    h.getPos(a, e); NEAR(a, 45, 1e-9);
    // Gira el barco 2 grados: no se mueve; 5 grados: vuelve a apuntar (no antes de 1 s)
    h.setHeading(92, 1500); NEAR(f.az, 315, 1e-9);
    h.setHeading(95, 1800); NEAR(f.az, 315, 1e-9);           // aun no ha pasado 1 s
    h.setHeading(95, 2100); NEAR(f.az, 310, 1e-9);
    // Solapamiento (0-450): de las vueltas posibles, la mas cercana a donde esta
    f.az = 400; h.setHeading(0, 3000);
    CHECK(h.setAz(30)); NEAR(f.az, 390, 1e-9);                // 30 o 390: 390 esta a 10 grados
    f.az = 10; CHECK(h.setAz(30)); NEAR(f.az, 30, 1e-9);
    // Parar, aparcar o mover a mano dejan de seguir el rumbo
    CHECK(h.stop() && !h.hasTarget());
    const double before = f.az;
    h.setHeading(60, 9000); NEAR(f.az, before, 1e-9);
    CHECK(h.setPos(90, 0)); NEAR(f.az, 30, 1e-9);
    CHECK(h.move(MOVE_LEFT, 50) && !h.hasTarget());
    // Rumbo viejo: sigue con el ultimo y lo dice
    CHECK(!h.stale(15000) && h.stale(20001) && h.ageS(20001) == 11);
    // Detras de WindRotor: aparcado por viento rechaza y deja de seguir
    WindGuard g; g.cfg.on = true; g.cfg.parkMs = 15;
    WindRotor w{g}; w.attach(&f);
    HeadingRotor h2; h2.enabled = true; h2.attach(&w); h2.setHeading(10, 0);
    CHECK(h2.setPos(100, 0)); NEAR(f.az, 90, 1e-9);
    g.update(20, 25, -1, 0, "lan", 100);
    CHECK(!h2.setPos(200, 0) && !h2.hasTarget());
    // Rotor de 0-360 sin solapamiento: 350 - 10 = 340
    FakeRotor f3; f3.maxA = 360; HeadingRotor h3; h3.enabled = true; h3.attach(&f3); h3.setHeading(10, 0);
    CHECK(h3.setAz(350)); NEAR(f3.az, 340, 1e-9);
    NEAR(angDiff(350, 10), 20, 1e-9); NEAR(norm360(-725), 355, 1e-9);
}

// Anemómetro de cazoletas: impulsos -> viento, medio, racha
static void testAnemo() {
    Anemometer a;
    uint32_t total = 0, t = 0;
    a.sample(total, t);
    for (int i = 0; i < 60; i++) { t += 1000; total += 10; a.sample(total, t); }        // 10 Hz durante 1 min
    CHECK(a.have() && a.seconds() == 60);
    NEAR(a.hz(), 10, 1e-9); NEAR(a.windMs(), 10 * 2.4 / 3.6, 1e-9); NEAR(a.avgMs(120), 10 * 2.4 / 3.6, 1e-9);
    for (int i = 0; i < 3; i++) { t += 1000; total += 30; a.sample(total, t); }          // racha: 30 Hz durante 3 s
    for (int i = 0; i < 10; i++) { t += 1000; total += 10; a.sample(total, t); }
    NEAR(a.gustMs(), 30 * 2.4 / 3.6, 1e-9); NEAR(a.windMs(), 10 * 2.4 / 3.6, 1e-9);
    NEAR(a.avgMs(73), (70 * 10 + 90) / 73.0 * 2.4 / 3.6, 1e-9);
    t += 500; total += 99; a.sample(total, t);                                             // menos de 1 s: espera
    CHECK(a.seconds() == 73);
    t += 1000; a.sample(total, t);                                                         // 1,5 s con 99 impulsos = 66 Hz
    NEAR(a.hz(1), 66, 1e-9);
    t += 10000; a.sample(total, t); CHECK(!a.have());                                      // hueco de 10 s: de nuevo
    for (int i = 0; i < 3; i++) { t += 1000; a.sample(total, t); }
    CHECK(a.have() && a.windMs() == 0 && a.gustMs() == 0);
    a.kmhPerHz = 3.62;                                                                     // Davis
    CHECK(Anemometer::beaufort(0.2) == 0 && Anemometer::beaufort(5.0) == 3 && Anemometer::beaufort(17.2) == 8 && Anemometer::beaufort(40) == 12);
    ReedDebounce r;
    CHECK(r.accept(10000) && !r.accept(12000) && r.accept(15001) && r.accept(0xFFFFFFF0u) && r.accept(5000));   // vuelta del contador
}

static void testApi() {
    FakeHost h;
    ApiSession s;
    std::vector<std::string> o;
    s.handle("{\"c\":\"info\"}", h, o);
    CHECK(o.size() == 1 && o[0].find("\"e\":\"info\"") != std::string::npos && o[0].find("\"maxAz\":450") != std::string::npos && o[0].find("\"pin\":true") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"pos\",\"az\":10,\"el\":20}", h, o);
    CHECK(o[0] == "{\"e\":\"err\",\"c\":\"pos\",\"m\":\"auth required\"}");
    o.clear(); s.handle("{\"c\":\"auth\",\"pin\":\"000000\"}", h, o); CHECK(o[0].find("bad pin") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"auth\",\"pin\":\"123456\"}", h, o); CHECK(o[0] == "{\"e\":\"ok\",\"c\":\"auth\"}");
    o.clear(); s.handle("{\"c\":\"pos\",\"az\":250.5,\"el\":300}", h, o); CHECK(o[0] == "{\"e\":\"ok\",\"c\":\"pos\"}");
    NEAR(h.r.az, 250.5, 1e-9); NEAR(h.r.el, 180, 1e-9);
    o.clear(); s.handle("{\"c\":\"get\"}", h, o);
    CHECK(o[0].find("\"az\":250.5") != std::string::npos && o[0].find("\"mv\":true") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"move\",\"d\":\"ccw\",\"s\":30}", h, o); CHECK(h.r.lastMove == MOVE_LEFT && h.r.lastSpeed == 30);
    o.clear(); s.handle("{\"c\":\"move\",\"d\":\"sideways\"}", h, o); CHECK(o[0].find("err") != std::string::npos);
    o.clear(); s.handle("no json", h, o); CHECK(o[0].find("bad json") != std::string::npos);
    // Configuracion por trozos: se lee y se vuelve a subir igual
    o.clear(); s.handle("{\"c\":\"cfgget\"}", h, o);
    CHECK(o.size() == (h.cfg.size() + CFG_CHUNK - 1) / CFG_CHUNK);
    std::string back;
    for (auto& l : o) {
        JsonDocument d; deserializeJson(d, l);
        back += d["d"].as<const char*>();
        CHECK(l.size() < 244);
    }
    CHECK(back == h.cfg);
    std::vector<std::string> pieces = o, o2;
    for (auto& l : pieces) {
        JsonDocument d; deserializeJson(d, l);
        d["c"] = "cfgset"; d.remove("e");
        std::string line; serializeJson(d, line);
        s.handle(line, h, o2);
    }
    CHECK(o2.back().find("\"done\":true") != std::string::npos && h.applied == h.cfg);
    o2.clear(); s.handle("{\"c\":\"cfgset\",\"i\":1,\"n\":3,\"d\":\"x\"}", h, o2); CHECK(o2[0].find("order") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"reboot\"}", h, o); CHECK(h.rebooted);
    o.clear(); s.handle("{\"c\":\"raw\"}", h, o); CHECK(o[0] == "{\"e\":\"raw\",\"az\":1234}");
}


// ------------------------------------------------------------------------------------------------- CAT
static std::string hexs(const std::string& s) { std::string o; char b[4]; for (unsigned char c : s) { snprintf(b, sizeof b, "%02X ", c); o += b; } return o; }
static void feedS(CatEngine& e, const std::string& s, uint32_t t) { e.feed((const uint8_t*)s.data(), s.size(), t); }

static void testCatCiv() {
    CivEngine e; e.addr = 0xA2;
    std::string out;
    e.tick(0, out);
    CHECK(hexs(out) == "FE FE A2 E0 03 FD ");                       // consulta de frecuencia
    feedS(e, out, 5);                                                // eco del bus: se ignora
    CHECK(!e.state().link);
    feedS(e, std::string("\xFE\xFE\xE0\xA2\x03\x00\x50\x82\x45\x01\xFD", 11), 20);
    CHECK(e.state().link && e.state().freq == 145825000ULL);
    out.clear(); e.tick(200, out); CHECK(hexs(out) == "FE FE A2 E0 04 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\x04\x05\x01\xFD", 8), 210);
    CHECK(e.state().mode == "FM");
    out.clear(); e.tick(400, out); CHECK(hexs(out) == "FE FE A2 E0 1C 00 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\x1C\x00\x01\xFD", 8), 410); CHECK(e.state().ptt);
    out.clear(); e.tick(600, out); CHECK(hexs(out) == "FE FE A2 E0 15 02 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\x15\x02\x01\x20\xFD", 9), 610); CHECK(e.state().smeter == 9);
    out.clear(); e.tick(800, out); CHECK(hexs(out) == "FE FE A2 E0 1A 06 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\x1A\x06\x01\x01\xFD", 9), 810); CHECK(e.state().mode == "PKTFM");
    out.clear(); e.tick(1000, out); CHECK(hexs(out) == "FE FE A2 E0 25 01 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\xFA\xFD", 6), 1010);  // NG: sin orden 0x25, no se vuelve a pedir
    out.clear(); e.tick(1200, out); CHECK(hexs(out) == "FE FE A2 E0 03 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\x03\x00\x00\x07\x14\x00\xFD", 11), 1210);
    CHECK(e.state().freq == 14070000ULL);
    // Órdenes
    CHECK(e.setFreq(145800000ULL));
    out.clear(); e.tick(1400, out); CHECK(hexs(out) == "FE FE A2 E0 05 00 00 80 45 01 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\xFB\xFD", 6), 1405);
    CHECK(e.setMode("DIG"));
    out.clear(); e.tick(1500, out); CHECK(hexs(out) == "FE FE A2 E0 06 01 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\xFB\xFD", 6), 1505);
    out.clear(); e.tick(1600, out); CHECK(hexs(out) == "FE FE A2 E0 1A 06 01 01 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\xFB\xFD", 6), 1605);
    CHECK(!e.setPtt(true));                                          // PTT no permitido
    e.allowPtt = true; CHECK(e.setPtt(true));
    out.clear(); e.tick(1700, out); CHECK(hexs(out) == "FE FE A2 E0 1C 00 01 FD ");
    feedS(e, std::string("\xFE\xFE\xE0\xA2\xFB\xFD", 6), 1705);
    CHECK(e.setTxFreq(435100000ULL));
    out.clear(); e.tick(1800, out); CHECK(hexs(out) == "FE FE A2 E0 25 01 00 00 10 35 04 FD ");
    // Transceive: la emisora manda la frecuencia sola (a 00)
    feedS(e, std::string("\xFE\xFE\x00\xA2\x00\x00\x25\x83\x45\x01\xFD", 11), 1810);
    CHECK(e.state().freq == 145832500ULL);
    // Sin respuesta: se pierde el enlace
    CivEngine m; out.clear();
    for (uint32_t t = 0; t < 4000; t += 50) m.tick(t, out);
    CHECK(!m.state().link);
    NEAR(CivEngine::unbcd((const uint8_t*)"\x00\x00\x00\x00\x10", 5), 1000000000.0, 0.5);
}

static void testCatYaesu() {
    YaesuEngine e;
    std::string out;
    e.tick(0, out); CHECK(out == "FA;");
    feedS(e, "FA145825000;", 10); CHECK(e.state().link && e.state().freq == 145825000ULL);
    out.clear(); e.tick(200, out); CHECK(out == "MD0;");
    feedS(e, "MD04;", 210); CHECK(e.state().mode == "FM");
    out.clear(); e.tick(400, out); CHECK(out == "TX;");
    feedS(e, "TX0;", 410); CHECK(!e.state().ptt);
    out.clear(); e.tick(600, out); CHECK(out == "SM0;");
    feedS(e, "SM0120;", 610); CHECK(e.state().smeter == 9);
    out.clear(); e.tick(800, out); CHECK(out == "FB;");
    feedS(e, "FB435100000;", 810); CHECK(e.state().txFreq == 435100000ULL);
    CHECK(e.setFreq(7074000)); out.clear(); e.tick(1000, out); CHECK(out == "FA007074000;");
    CHECK(e.setMode("PKTUSB")); out.clear(); e.tick(1100, out); CHECK(out == "MD0C;");
    CHECK(!e.setMode("XYZ"));
    feedS(e, "?;", 1110);
    e.allowPtt = true; e.setPtt(true); out.clear(); e.tick(1200, out); CHECK(out == "TX1;");
    feedS(e, "MD0C;", 1300); CHECK(e.state().mode == "PKTUSB");
}

static void testCatKenwood() {
    KenwoodEngine e;
    std::string out;
    e.tick(0, out); CHECK(out == "IF;");
    // IF: frecuencia, 5 huecos, RIT, RIT, XIT, banco, memoria, TX, modo, VFO, barrido, split, tono, nº tono, desplazamiento
    feedS(e, "IF00014074000     +00000000012001000000;", 10);
    CHECK(e.state().link && e.state().freq == 14074000ULL && e.state().ptt && e.state().mode == "USB" && e.state().vfo == 'A' && e.state().split);
    out.clear(); e.tick(200, out); CHECK(out == "SM0;");
    feedS(e, "SM00015;", 210); CHECK(e.state().smeter == 9);
    out.clear(); e.tick(400, out); CHECK(out == "FB;");
    feedS(e, "FB00014076000;", 410); CHECK(e.state().txFreq == 14076000ULL && e.state().freq == 14074000ULL);
    out.clear(); e.tick(600, out); CHECK(out == "DA;");
    feedS(e, "DA1;", 610); CHECK(e.state().mode == "PKTUSB");
    CHECK(e.setFreq(145800000)); out.clear(); e.tick(800, out); CHECK(out == "FA00145800000;");
    CHECK(e.setMode("CW")); out.clear(); e.tick(900, out); CHECK(out == "MD3;");
    out.clear(); e.tick(950, out); CHECK(out == "DA0;");
    CHECK(e.toggleVfo()); out.clear(); e.tick(1000, out); CHECK(out == "FR1;FT1;" && e.state().vfo == 'B');
    feedS(e, "SM0030;", 1010); CHECK(e.state().smeter == 15);
}

static void testCatYaesu5() {
    Yaesu5Engine e;
    std::string out;
    e.tick(0, out); CHECK(hexs(out) == "00 00 00 00 03 ");
    feedS(e, std::string("\x14\x52\x50\x00\x01", 5), 20);
    CHECK(e.state().link && e.state().freq == 145250000ULL && e.state().mode == "USB");
    out.clear(); e.tick(200, out); CHECK(hexs(out) == "00 00 00 00 E7 ");
    feedS(e, std::string("\x09", 1), 210); CHECK(e.state().smeter == 9);
    out.clear(); e.tick(400, out); CHECK(hexs(out) == "00 00 00 00 F7 ");
    feedS(e, std::string("\x7F", 1), 410); CHECK(e.state().ptt);
    CHECK(e.setFreq(145825000)); out.clear(); e.tick(600, out); CHECK(hexs(out) == "14 58 25 00 01 ");
    CHECK(e.setMode("FM")); out.clear(); e.tick(700, out); CHECK(hexs(out) == "08 00 00 00 07 ");
    CHECK(!e.setTxFreq(435000000));
    e.allowPtt = true; e.setPtt(false); out.clear(); e.tick(800, out); CHECK(hexs(out) == "00 00 00 00 88 ");
}

// CTCSS de la subida (satélites FM): las órdenes de cada marca
static void testCatTone() {
    CHECK(toneIndex(CTCSS50, 50, 670) == 0 && toneIndex(CTCSS50, 50, 744) == 3 && toneIndex(CTCSS50, 50, 1598) == 26 && toneIndex(CTCSS50, 50, 2541) == 49);
    CHECK(toneIndex(CTCSS42, 41, 1622) == 26 && toneIndex(CTCSS42, 41, 1598) == -1 && toneIndex(CTCSS42, 41, 671) == 0);
    {   // Icom: 1B 00 00 06 70 y 16 42 01
        CivEngine e; e.addr = 0xA2; std::string out;
        CHECK(e.setTone(670) && e.state().tone == 670);
        e.tick(0, out); CHECK(hexs(out) == "FE FE A2 E0 1B 00 00 06 70 FD ");
        feedS(e, std::string("\xFE\xFE\xE0\xA2\xFB\xFD", 6), 10);
        out.clear(); e.tick(100, out); CHECK(hexs(out) == "FE FE A2 E0 16 42 01 FD ");
        feedS(e, std::string("\xFE\xFE\xE0\xA2\xFB\xFD", 6), 110);
        CHECK(e.setTone(0) && e.state().tone == 0);
        out.clear(); e.tick(200, out); CHECK(hexs(out) == "FE FE A2 E0 16 42 00 FD ");
        CHECK(!e.setTone(3000));
    }
    {   // Yaesu de texto: CN00nnn; CT02;
        YaesuEngine e; std::string out;
        CHECK(e.setTone(744)); e.tick(0, out); CHECK(out == "CN00003;CT02;");
        CHECK(e.setTone(0)); out.clear(); e.tick(100, out); CHECK(out == "CT00;");
        CHECK(!e.setTone(1500));
    }
    {   // Kenwood: TS-2000 desde 01, TS-590 desde 00
        KenwoodEngine e; std::string out;
        CHECK(e.setTone(670)); e.tick(0, out); CHECK(out == "TN01;TO1;");
        e.toneBase = 0; CHECK(e.setTone(885)); out.clear(); e.tick(100, out); CHECK(out == "TN08;TO1;");
        CHECK(e.setTone(0)); out.clear(); e.tick(200, out); CHECK(out == "TO0;");
    }
    {   // FT-817: 08 85 08 85 0B y 4A 00 00 00 0A
        Yaesu5Engine e; std::string out;
        CHECK(e.setTone(885)); e.tick(0, out); CHECK(hexs(out) == "08 85 08 85 0B ");
        out.clear(); e.tick(100, out); CHECK(hexs(out) == "4A 00 00 00 0A ");
        CHECK(e.setTone(0)); out.clear(); e.tick(200, out); CHECK(hexs(out) == "8A 00 00 00 0A ");
    }
    {   // rigctl: C 670 y U TONE 1
        RigctlClientEngine e; std::string out;
        e.onConnect(0);
        CHECK(e.setTone(670)); e.tick(0, out); CHECK(out == "C 670\n");
        feedS(e, "RPRT 0\n", 10);
        out.clear(); e.tick(100, out); CHECK(out == "U TONE 1\n");
    }
    {   // rigctld de la caja: c / C / u / U
        YaesuEngine e; RigctlSession s; std::string r;
        s.feed("C 670\n", 6, e, r); CHECK(r == "RPRT 0\n" && e.state().tone == 670);
        r.clear(); s.feed("c\n", 2, e, r); CHECK(r == "670\n");
        r.clear(); s.feed("U TONE 0\n", 9, e, r); CHECK(r == "RPRT 0\n" && e.state().tone == 0);
        r.clear(); s.feed("u TONE\n", 7, e, r); CHECK(r == "0\n");
        r.clear(); s.feed("U TONE 1\n", 9, e, r); CHECK(r == "RPRT 0\n" && e.state().tone == 670);
        r.clear(); s.feed("U NB 1\n", 7, e, r); CHECK(r == "RPRT -4\n");
    }
}

// Bus RS485 con direcciones (pasarela 4)
static void testRsBus() {
    using namespace RsBus;
    CHECK(lastNumber("0") == 0 && lastNumber("160,50") == 160.5 && lastNumber("151,30;20") == 20 && lastNumber("KW Beam") == 0);
    CHECK(lastNumber("-3.5") == -3.5 && lastNumber("A1;B2,25") == 2.25);
    CHECK(fmtNum(27) == "27" && fmtNum(187.5) == "187,5" && fmtNum(23.25) == "23,25");
    CHECK(frame(3, 20, "GETPOSDG", "0") == "#3:20:GETPOSDG:0:23$");
    CHECK(frame(3, 20, "SETPOSDG", fmtDeg(160.5)) == "#3:20:SETPOSDG:160,50:183,5$");
    CHECK(frame(3, 21, "STOP", "0") == "#3:21:STOP:0:24$");
    Msg m;
    CHECK(parse("20:3:ACK_GETPOSDG:372,04:395,04", m) && m.src == 20 && m.dst == 3 && m.cmd == "ACK_GETPOSDG" && fabs(num(m.params) - 372.04) < 1e-9);
    CHECK(parse("20:3:ACK_GETPOSDG:160,00:183.00", m));            // suma con punto y ceros
    CHECK(!parse("20:3:ACK_GETPOSDG:160,00:184", m));              // suma mala
    CHECK(!parse("20:3:ACK_GETPOSDG", m) && !parse("x:3:A:1:4", m));
    Reader rd; int got = 0;
    const char* stream = "basura#20:255:ERR:16:291$##21:3:ACK_STOP:1:25$#20:3:NAK_SETPOSDG:1:9$";
    std::vector<Msg> ms;
    for (const char* p = stream; *p; p++) if (rd.push(*p, m)) { got++; ms.push_back(m); }
    CHECK(got == 2 && ms[0].cmd == "ERR" && ms[0].dst == 255 && num(ms[0].params) == 16 && ms[1].cmd == "ACK_STOP");
    CHECK(std::string(serialProtoName(SP_RSBUS)) == "RS485 bus");
}

static void testRigctld() {
    YaesuEngine e;
    std::string out;
    e.tick(0, out); feedS(e, "FA145825000;", 5);
    out.clear(); e.tick(200, out); feedS(e, "MD04;", 205);
    RigctlSession s;
    std::string r;
    s.feed("f\n", strlen("f\n"), e, r); CHECK(r == "145825000\n");
    r.clear(); s.feed("m\n", strlen("m\n"), e, r); CHECK(r == "FM\n15000\n");
    r.clear(); s.feed("F 145800000\n", strlen("F 145800000\n"), e, r); CHECK(r == "RPRT 0\n" && e.state().freq == 145800000ULL);
    r.clear(); s.feed("\\set_freq 145790000.000000\n", strlen("\\set_freq 145790000.000000\n"), e, r); CHECK(r == "RPRT 0\n" && e.state().freq == 145790000ULL);
    r.clear(); s.feed("T 1\n", strlen("T 1\n"), e, r); CHECK(r == "RPRT -9\n");          // PTT no permitido
    r.clear(); s.feed("M USB 2400\n", strlen("M USB 2400\n"), e, r); CHECK(r == "RPRT 0\n" && e.state().mode == "USB");
    r.clear(); s.feed("I 435100000\n", strlen("I 435100000\n"), e, r); CHECK(r == "RPRT 0\n" && e.state().txFreq == 435100000ULL);
    r.clear(); s.feed("i\n", strlen("i\n"), e, r); CHECK(r == "435100000\n");
    r.clear(); s.feed("v\n", strlen("v\n"), e, r); CHECK(r == "VFOA\n");
    r.clear(); s.feed("l STRENGTH\n", strlen("l STRENGTH\n"), e, r); CHECK(r == "-54\n");
    r.clear(); s.feed("\\chk_vfo\n", strlen("\\chk_vfo\n"), e, r); CHECK(r == "0\n");
    r.clear(); s.feed("\\dump_state\n", strlen("\\dump_state\n"), e, r);
    CHECK(r.compare(0, 6, "0\n2\n2\n") == 0 && r.find("0 0 0 0 0 0 0\n") != std::string::npos);
    int lines = 0; for (char c : r) if (c == '\n') lines++;
    CHECK(lines > 30);
    r.clear(); s.feed("x\n", strlen("x\n"), e, r); CHECK(r == "USB\n2400\n");
    r.clear(); s.feed("Z\n", strlen("Z\n"), e, r); CHECK(r == "RPRT -4\n");
    bool closed = s.feed("q\n", strlen("q\n"), e, r); CHECK(closed);
}

static void testCatTracker() {
    NEAR((double)dopplerRx(145800000ULL, 7.0), 145800000.0 - 145800000.0 * 7.0 / 299792.458, 1.0);
    NEAR((double)dopplerTx(437800000ULL, -7.0), 437800000.0 - 437800000.0 * 7.0 / 299792.458, 1.0);
    YaesuEngine e;
    std::string out;
    e.tick(0, out); feedS(e, "FA145800000;", 5);
    CatTracker t;
    t.update("ISS", 145800000ULL, 145990000ULL, -6.5, 100);
    t.step(e, 1000);
    CHECK(e.state().freq == dopplerRx(145800000ULL, -6.5) && e.state().txFreq == dopplerTx(145990000ULL, -6.5));
    CHECK(t.doppler > 3000 && t.doppler < 3300);
    const size_t q = e.queued();
    t.update("ISS", 145800000ULL, 145990000ULL, -6.5001, 1200);
    t.step(e, 1600);
    CHECK(e.queued() == q);                                          // cambio menor que el paso: no manda nada
    t.enabled = false; t.update("ISS", 145800000ULL, 0, 5.0, 2000); t.step(e, 3000);
    CHECK(e.queued() == q);
    t.enabled = true; t.step(e, 30000);
    CHECK(!t.active);                                                // sin datos: se apaga
    CHECK(normMode("dig") == "PKTUSB" && normMode("fm-n") == "FM" && normMode("CW-L") == "CWR");
    CHECK(smeterFrom255(0) == 0 && smeterFrom255(120) == 9 && smeterFrom255(241) == 15 && smeterFrom30(30) == 15 && smeterDb(9) == 0);
}

struct CatHost : FakeHost {
    YaesuEngine y;
    CatTracker tr;
    bool pa = false;
    Rig* rig() override { return &y; }
    CatTracker* catTracker() override { return &tr; }
    bool catPttAllowed() override { return pa; }
    uint32_t nowMs() override { return 5000; }
};

static void testCatApi() {
    CatHost h;
    ApiSession s;
    std::vector<std::string> o;
    std::string out;
    h.y.tick(0, out); feedS(h.y, "FA145825000;", 5);
    s.handle("{\"c\":\"auth\",\"pin\":\"123456\"}", h, o);
    o.clear(); s.handle("{\"c\":\"cat\"}", h, o);
    if (o[0].find("145825000") == std::string::npos) printf("%s\n", o[0].c_str());
    CHECK(o[0].find("\"e\":\"cat\"") != std::string::npos && o[0].find("\"f\":145825000") != std::string::npos && o[0].find("\"l\":true") != std::string::npos);
    o.clear(); s.handle("{\"c\":\"catset\",\"f\":145800000,\"m\":\"FM\"}", h, o);
    CHECK(o[0] == "{\"e\":\"ok\",\"c\":\"catset\"}" && h.y.state().freq == 145800000ULL && h.y.state().mode == "FM");
    o.clear(); s.handle("{\"c\":\"catset\",\"ptt\":true}", h, o); CHECK(o[0].find("ptt not allowed") != std::string::npos);
    h.pa = true; h.y.allowPtt = true;
    o.clear(); s.handle("{\"c\":\"catset\",\"ptt\":true}", h, o); CHECK(o[0].find("ok") != std::string::npos && h.y.state().ptt);
    o.clear(); s.handle("{\"c\":\"cattrack\",\"name\":\"ISS\",\"dl\":145800000,\"ul\":145990000,\"rr\":-6.5}", h, o);
    CHECK(o[0].find("ok") != std::string::npos && h.tr.active && h.tr.name == "ISS" && h.tr.upd == 5000);
    o.clear(); s.handle("{\"c\":\"cattrack\",\"on\":false}", h, o); CHECK(!h.tr.enabled);
    o.clear(); s.handle("{\"c\":\"info\"}", h, o); CHECK(o[0].find("\"cat\":true") != std::string::npos);
    FakeHost nh; ApiSession s2;
    o.clear(); s2.handle("{\"c\":\"cat\"}", nh, o); CHECK(o[0].find("no cat") != std::string::npos);
}

static std::string wsServer(int op, const std::string& p) {        // trama del servidor (sin máscara)
    std::string f; f += (char)(0x80 | op);
    if (p.size() < 126) f += (char)p.size(); else if (p.size() < 65536) { f += (char)126; f += (char)(p.size() >> 8); f += (char)(p.size() & 255); }
    else { f += (char)127; for (int i = 7; i >= 0; i--) f += (char)((uint64_t)p.size() >> (8 * i)); }
    return f + p;
}
static std::string wsUnmask(const std::string& f) {                  // trama del cliente -> texto
    size_t hl = 2; size_t n = (uint8_t)f[1] & 0x7F;
    if (n == 126) { n = ((uint8_t)f[2] << 8) | (uint8_t)f[3]; hl = 4; }
    std::string p = f.substr(hl + 4, n);
    for (size_t i = 0; i < p.size(); i++) p[i] ^= f[hl + (i % 4)];
    return p;
}
static std::vector<std::string> wsSplit(const std::string& s) {
    std::vector<std::string> v; size_t i = 0;
    while (i + 2 <= s.size()) { size_t n = (uint8_t)s[i + 1] & 0x7F, hl = 2; if (n == 126) { n = ((uint8_t)s[i + 2] << 8) | (uint8_t)s[i + 3]; hl = 4; } v.push_back(wsUnmask(s.substr(i, hl + 4 + n))); i += hl + 4 + n; }
    return v;
}

static void testSdrConnect() {
    SdrConnectEngine e;
    e.onConnect(0);
    std::string out;
    e.tick(0, out);
    CHECK(out.compare(0, 16, "GET / HTTP/1.1\r\n") == 0 && out.find("Upgrade: websocket") != std::string::npos && out.find("Sec-WebSocket-Key:") != std::string::npos);
    CHECK(!e.setFreq(145800000));                                    // aún sin WebSocket
    feedS(e, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n", 50);
    CHECK(e.handshaken() && e.state().link);
    out.clear();
    for (uint32_t t = 60; t < 400; t += 10) e.tick(t, out);
    std::vector<std::string> m = wsSplit(out);
    CHECK(m.size() >= 5 && m[0] == "{\"event_type\":\"get_property\",\"property\":\"device_vfo_frequency\",\"device\":\"primary\",\"value\":\"\"}");
    feedS(e, wsServer(1, "{\"event_type\":\"property_changed\",\"property\":\"device_vfo_frequency\",\"value\":\"145800000\"}"), 500);
    feedS(e, wsServer(1, "{\"event_type\":\"get_property_response\",\"property\":\"demodulator\",\"value\":\"NFM\"}"), 510);
    feedS(e, wsServer(2, std::string(5000, '\x11')), 520);         // espectro: se salta
    feedS(e, wsServer(1, "{\"event_type\":\"property_changed\",\"property\":\"signal_power\",\"value\":-73.2}"), 530);
    feedS(e, wsServer(1, "{\"event_type\":\"get_property_response\",\"property\":\"device_center_frequency\",\"value\":\"145000000\"}"), 540);
    feedS(e, wsServer(1, "{\"event_type\":\"get_property_response\",\"property\":\"device_sample_rate\",\"value\":\"2000000\"}"), 550);
    CHECK(e.state().freq == 145800000ULL && e.state().mode == "FM" && e.state().smeter == 9);
    out.clear(); e.tick(2000, out); out.clear();
    CHECK(e.setFreq(146500000ULL) && e.setMode("USB"));
    for (uint32_t t = 2100; t < 2300; t += 30) e.tick(t, out);
    m = wsSplit(out);
    CHECK(m.size() >= 3 && m[0].find("device_center_frequency") != std::string::npos && m[0].find("146500000") != std::string::npos);
    CHECK(m[1].find("\"device_vfo_frequency\",\"device\":\"primary\",\"value\":\"146500000\"") != std::string::npos && m[2].find("\"value\":\"USB\"") != std::string::npos);
    e.allowPtt = true; CHECK(!e.setPtt(true) && !e.setTxFreq(435000000));     // receptor
    out.clear(); feedS(e, wsServer(9, "hi"), 3000); e.tick(3100, out);
    CHECK(!out.empty() && ((uint8_t)out[0] & 0x0F) == 10 && wsUnmask(out) == "hi");   // pong
    CHECK(!e.stale(5000) && e.stale(10000));
    CHECK(jsonField("{\"a\":\"x\\\"y\",\"b\": 12.5}", "a") == "x\"y" && jsonField("{\"a\":1,\"b\": 12.5}", "b") == "12.5");
}

static void testRigctlClient() {
    RigctlClientEngine e;
    e.onConnect(0);
    std::string out;
    e.tick(0, out); CHECK(out == "f\n");
    feedS(e, "145800000\n", 10); CHECK(e.state().link && e.state().freq == 145800000ULL);
    out.clear(); e.tick(300, out); CHECK(out == "m\n");
    feedS(e, "FM\n15000\n", 310); CHECK(e.state().mode == "FM");
    out.clear(); e.tick(600, out); CHECK(out == "l STRENGTH\n");
    feedS(e, "RPRT -11\n", 610);                                    // SDR++ no lo tiene: no se vuelve a pedir
    out.clear(); e.tick(900, out); CHECK(out == "t\n");
    feedS(e, "0\n", 910);
    out.clear(); e.tick(1200, out); CHECK(out == "i\n");
    feedS(e, "RPRT -4\n", 1210);
    out.clear(); e.tick(1500, out); CHECK(out == "f\n"); feedS(e, "145800000\n", 1510);
    out.clear(); e.tick(1800, out); CHECK(out == "m\n"); feedS(e, "USB\n2400\n", 1810);
    out.clear(); e.tick(2100, out); CHECK(out == "f\n");            // sin «l»: repite la frecuencia
    feedS(e, "145800000\n", 2110);
    CHECK(e.setFreq(145825000)); out.clear(); e.tick(2400, out); CHECK(out == "F 145825000\n");
    feedS(e, "RPRT 0\n", 2410);
    CHECK(e.setMode("dig")); out.clear(); e.tick(2500, out); CHECK(out == "M PKTUSB 0\n");
}

int main() {
    testRotctld();
    testGs232();
    testEasyComm();
    testRot2Prog();
    testControl();
    testSensorless();
    testTotp();
    testAprs();
    testApi();
    testWind();
    testHeading();
    testAnemo();
    testCatCiv();
    testCatYaesu();
    testCatKenwood();
    testCatYaesu5();
    testCatTone();
    testRsBus();
    testRigctld();
    testCatTracker();
    testCatApi();
    testSdrConnect();
    testRigctlClient();
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
