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
#include "board.h"

#define ROTOR_VERSION "5.3.4"            // el mismo numero que el tracker APRS LoRa+ (todos los repositorios del proyecto van a la par)

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
    std::string ntp = "pool.ntp.org";

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
    int         adsGain = 1;                // 0: +-6,144 V  1: +-4,096 V  2: +-2,048 V
    double      parkAz = 0, parkEl = 0;
    bool        reportMech = false;

    // Pasarela serie
    int         serProto = 1;               // 0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog
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

    // Pantalla tactil (placas con LCD)
    int         lcdBright = 80;             // brillo 10-100 %
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
