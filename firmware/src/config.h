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
 */

#pragma once
// Configuracion del rotor: /rotor_conf.json en LittleFS. Se edita entera desde la WebUI o la app (cfgget / cfgset).
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include "rc_control.h"
#include "rc_aprs.h"
#include "rc_cat.h"
#include "board.h"

#define ROTOR_VERSION "5.4.5"            // el mismo numero que el tracker APRS LoRa+ (todos los repositorios del proyecto van a la par)

struct AxisPins {
    int  pinA      = -1;    // reles: CW / UP. Puente H: IN1 (PWM)
    int  pinB      = -1;    // reles: CCW / DOWN. Puente H: IN2 (PWM)
    int  pinPwm    = -1;    // velocidad por PWM (G-2800, DC con control de velocidad); -1 = sin velocidad
    int  sensor    = 0;     // 0 = ADC de la placa, 1 = ADS1115 (I2C), 2 = sin sensor (solo manual)
    int  adcPin    = -1;
    int  adsChannel = 0;
};

struct RotorConfig {
    std::string name = "APRS-Rotor";        // nombre en la red (mDNS), en Bluetooth y en la WebUI
    std::string pin  = "123456";            // PIN de control (app por BLE / TCP, WebUI). Vacio = sin PIN

    // Red
    int         wifiMode = 2;               // 0 apagada, 1 cliente (con punto de acceso de rescate), 2 punto de acceso
    std::string ssid, pass;
    std::string apPass = "aprsrotor";       // minimo 8 caracteres
    bool        rotctld = true;             // servidor rotctld (Hamlib NET rotctl, modelo 2)
    int         rotctldPort = 4533;
    int         apiPort = 4534;             // protocolo JSON por TCP (app por WiFi)
    std::string webUser = "admin";
    std::string webPass;                    // vacio = el PIN
    std::string ntp = "ntp.roa.es";         // hora oficial de Espana (ROA); de reserva hora.roa.es y pool.ntp.org. La del GPS manda

    // Puertos serie del lado del ordenador
    int         usbProto = 1;               // USB: 0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog, 4 rotctld (texto), 5 registro (log)
    int         btMode = 1;                 // 0 apagado, 1 BLE (app), 2 Bluetooth clasico SPP (tracker con GS-232)
    int         sppProto = 1;               // protocolo del SPP (como usbProto, 0-4)

    // Mando del rotor
    int         driver = 0;                 // 0 control directo, 1 pasarela serie a un controlador
    RC::AxisCfg az, el;
    AxisPins    azPins, elPins;
    bool        elEnabled = true;
    int         outMode = 0;                // 0 reles (nivel), 1 puente H (IN1 / IN2 con PWM)
    bool        activeHigh = true;          // nivel que activa los reles / el puente
    int         brakePin = -1;
    int         pwmFreq = 1000;
    int         i2cSda = DEF_I2C_SDA, i2cScl = DEF_I2C_SCL;
    int         adsAddr = 0x48;
    int         pcfAddr = 0x20;             // PCF8574 de una placa de relés en el I2C (pines 120-127)
    int         adsGain = 1;                // 0: +-6,144 V  1: +-4,096 V  2: +-2,048 V
    double      parkAz = 0, parkEl = 0;
    bool        reportMech = false;

    // Pasarela serie
    int         serProto = 1;               // 0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog, 4 bus RS485
    int         serBusId = 3;               // bus RS485: dirección de la placa (maestro)
    int         serAzId = 20;               // bus RS485: dirección del controlador del acimut
    int         serElId = 21;               // bus RS485: dirección del de elevación (con serHasEl)
    int         serBaud = 9600;
    int         serRx = DEF_SER_RX, serTx = DEF_SER_TX;
    bool        serHasEl = true;
    int         serPollMs = 500;
    double      serMinAz = 0, serMaxAz = 450, serMaxEl = 180;

    // LoRa
    int         loraChip = DEF_LORA_CHIP;   // 0 ninguno, 1 SX1262, 2 SX1268, 3 SX1276, 4 SX1278
    double      loraFreq = 433.775;
    double      loraBw = 125.0;
    int         loraSf = 12, loraCr = 5, loraPower = 17;
    double      loraTcxo = DEF_LORA_TCXO;
    int         loraSck = DEF_LORA_SCK, loraMiso = DEF_LORA_MISO, loraMosi = DEF_LORA_MOSI, loraCs = DEF_LORA_CS;
    int         loraRst = DEF_LORA_RST, loraIrq = DEF_LORA_IRQ, loraBusy = DEF_LORA_BUSY;

    // APRS (por LoRa)
    RC::AprsCtlCfg aprs;
    int         beaconMin = 0;              // baliza de estado cada N minutos (0 = no)
    double      lat = 0, lon = 0;           // posicion de la estacion (para la baliza)
    std::string comment = "APRS LoRa+ Rotor";

    int         led = DEF_LED;

    // CAT de la emisora (rc_cat.h)
    int         catProto = 0;               // 0 ninguno, 1 Icom CI-V, 2 Yaesu (texto), 3 Yaesu 5 bytes (FT-817/857/897), 4 Kenwood / Elecraft,
                                            // 5 SDRconnect (SDRplay RSPdx-R2...) y 6 rigctld (SDR++, gqrx, Hamlib) por red
    std::string catHost;                    // red: equipo con SDRconnect / SDR++ / rigctld
    int         catPort = 0;                // red: 0 = el de siempre (5454 SDRconnect, 4532 rigctl)
    int         catBaud = 19200;
    int         catRx = DEF_CAT_RX, catTx = DEF_CAT_TX;
    int         catCiv = 0xA2;              // direccion CI-V de la emisora (IC-9700 A2, IC-705 A4, IC-7300 94, IC-7610 98)
    std::string catModel;                   // nombre que se muestra ("IC-9700")
    int         catPollMs = 120;
    bool        catPtt = false;             // permitir PTT desde la red, la app y la pantalla
    int         catPttMaxS = 180;           // limite de emision continua: pasado este tiempo se suelta el PTT
    int         catTrackStep = 50;          // Hz: cambio minimo para resintonizar con el Doppler
    bool        rigctld = true;             // servidor rigctld (Hamlib NET rigctl) si hay CAT
    int         rigctldPort = 4532;

    // Proteccion por viento (tiempo de internet que manda la app; lib/RotorCore/src/rc_wind.h)
    bool        windOn = false;
    double      windParkMs = 15.0;          // racha (o viento medio) en m/s para aparcar y bloquear
    double      windResumeMs = 10.0;        // vuelve a aceptar movimientos por debajo de esto...
    int         windHoldMin = 15;           // ...y tras estos minutos sin rachas fuertes
    int         anemoPin = -1;              // anemómetro de cazoletas (ampolla reed a masa); -1 = sin él (se usa el tiempo de la app)
    double      anemoKmhHz = 2.4;           // km/h por impulso por segundo (Misol / Fine Offset 2,4; Davis 6410 3,62)
    bool        anemoPullup = true;         // resistencia interna a 3V3

    // Red local (src/lan.cpp, librerias lib/aprs_links y lib/plotter_core del plotter): datos del barco o de la estacion por la WiFi
    int         lanKind = 0;                // servidor TCP: 0 ninguno, 1 NMEA 0183, 2 gpsd, 3 Signal K
    std::string lanHost;                    // vacio = el equipo que da la WiFi (el movil con la app, servidor NMEA en el 10110)
    int         lanPort = 10110;            // 10110 NMEA, 2947 gpsd, 8375 Signal K
    int         lanUdp = 0;                 // escucha NMEA por UDP (instrumento de viento: 10110); 0 = no
    std::string owmKey;                     // OpenWeatherMap: viento de internet sin la app (en lan.pos o en aprs.lat / lon)
    bool        lanWind = true;             // viento de la red (MWV, Signal K) o de OpenWeatherMap para la proteccion por viento
    bool        lanPos = true;              // posicion de la red como posicion de la estacion (baliza APRS, tiempo de internet)
    bool        headComp = false;           // rotor en un barco o un coche: acimut verdadero con el rumbo de la red (rc_heading.h)
    double      headStep = 3.0;             // grados de cambio de rumbo para volver a apuntar
    double      headVar = 0.0;              // declinacion (grados, E positiva) para un compas que solo da rumbo magnetico (HDG / HDM)
    bool        headVarAuto = true;         // declinacion del Modelo Magnetico Mundial (WMM) en la posicion de la estacion; headVar sin posicion

    // Seguir el sol o la luna (orden "track", lib/RotorCore/src/rc_sky.h)
    double      skyStep = 1.0;              // grados de cambio para volver a apuntar
    double      skyMinEl = 0.0;             // por debajo de esta elevacion espera
    bool        skyRefr = true;             // con la refraccion media (lo que ve la antena)

    // Pantalla tactil (placas con LCD)
    int         lcdBright = 80;             // brillo 10-100 %
    int         lcdTheme = 0;               // paleta de la pantalla: 0 = la de la app Android, 1 = la de la WebUI (cabecera roja)
    int         lcdDimS = 300;              // segundos sin tocar hasta bajar el brillo al 10 % (0 = nunca)
};

extern RotorConfig Cfg;

namespace Config {
    void        defaults(RotorConfig& c);
    bool        load();                                     // false = no habia fichero (quedan los valores por defecto)
    bool        save();
    std::string toJson(const RotorConfig& c, bool secrets);
    bool        fromJson(const std::string& json, RotorConfig& c, std::string& err);
}
