/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+ Rotor. GPL-3.0-or-later.
 *
 * Pruebas del nucleo en el PC (sin placa):  make -C esp32/test_host
 */
#include <stdio.h>
#include <string>
#include <vector>
#include "rc_rotctld.h"
#include "rc_serialproto.h"
#include "rc_control.h"
#include "rc_aprs.h"
#include "rc_api.h"

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
    void info(JsonObject o) override { o["dev"] = "APRS LoRa+ Rotor"; o["ver"] = "0.1.0"; }
    std::string configJson() override { return cfg; }
    bool applyConfig(const std::string& j, std::string& e) override { JsonDocument d; if (deserializeJson(d, j)) { e = "json"; return false; } applied = j; return true; }
    bool calibrate(const char*, const char*, double, std::string&) override { return true; }
    bool setHere(const char*, double, std::string&) override { return true; }
    void raw(JsonObject o) override { o["az"] = 1234; }
    void clearFault() override {}
    void reboot() override { rebooted = true; }
    const char* pin() override { return "123456"; }
};

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
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
