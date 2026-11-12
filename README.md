# APRS LoRa+ Rotor

[![Donate · PayPal.me](https://img.shields.io/badge/Donate-paypal.me%2Fea7jrs-0070E0?logo=paypal&logoColor=white)](https://paypal.me/ea7jrs)

Controlador de rotores de antena **compatible con Hamlib** para el ecosistema **APRS LoRa+**: lo manejan el [tracker APRS LoRa+](https://github.com/EA7JRS/APRS-LoRa-Plus), la app Android APRS LoRa+ y cualquier programa con Hamlib (GPredict, Look4Sat, PstRotator…).

**Manual**: [español](docs/MANUAL_ES.md) · [English](docs/MANUAL_EN.md) · **Guía rápida / Quick start**: [ES](docs/QUICKSTART_ES.md) · [EN](docs/QUICKSTART_EN.md)

**Placas**: ESP32, ESP32-S3, **Waveshare ESP32-C5-Touch-LCD-3.5**, **ESP32-S3-Touch-AMOLED-2.41** y **ESP32-S3-Touch-LCD-7B** (con pantalla táctil: mando del rotor en la propia caja), Raspberry Pi Pico 2 / Pico 2 W y Raspberry Pi (Zero 2 W, 3, 4, 5).

```
                       ┌──────────────── APRS LoRa+ Rotor ────────────────┐
 Tracker APRS LoRa+ ──►│ rotctld (WiFi 4533)   · GS-232B por SPP           │
                       │ mensajes APRS por LoRa (P az el + TOTP)           │──► rotor
 App Android ─────────►│ BLE / TCP 4534: protocolo JSON (PIN)              │    · control directo (relés / puente H + potenciómetro)
 GPredict, Look4Sat ──►│ rotctld (Hamlib NET rotctl, modelo 2)             │    · pasarela serie (GS-232, Rot2Prog, EasyComm, RS485)
 PC con Hamlib (USB) ─►│ se hace pasar por GS-232A/B, EasyComm II, Rot2Prog│    · Raspberry: rotctld de Hamlib (cualquier modelo)
 GPredict, WSJT-X ───►│ rigctld 4532 (CAT)  ──────────────────────────────│──► emisora / SDR (CI-V, Yaesu, Kenwood, SDRconnect)
 Navegador ───────────►│ WebUI (brújula, mando, calibración, config, OTA)  │
                       └───────────────────────────────────────────────────┘
```

## Qué hace cada placa

| | ESP32 | ESP32-S3 | ESP32-C5 LCD 3,5" | ESP32-S3 AMOLED 2,41" | ESP32-S3 LCD 7" | Pico 2 W | Pico 2 | Raspberry Pi |
|---|---|---|---|---| --- |---|---|---|
| Control directo (relés / puente H, ADC / ADS1115) | ✔ | ✔ | relés (EXIO8-15) + EXIO_ADC / ADS1115 | ✔ | relés por PCF8574 (I2C) + GPIO6 / ADS1115 | ✔ | ✔ | — |
| Pasarela serie a un controlador | ✔ | ✔ | ✔ (U0TXD / U0RXD) | ✔ (TXD / RXD) | ✔ (conector UART) | ✔ | ✔ | ✔ (rotctld de Hamlib: todos los modelos) |
| rotctld por WiFi, JSON por TCP, WebUI | ✔ | ✔ | ✔ (WiFi 2,4 y 5 GHz) | ✔ | ✔ | ✔ | — | ✔ |
| BLE para la app | ✔ | ✔ | ✔ | ✔ | ✔ | pendiente | — | ✔ |
| SPP (GS-232) para el tracker | ✔ | — | — | — | — | ✔ | — | ✔ |
| LoRa APRS | con módulo SX127x / SX126x | con módulo SX127x / SX126x | — | — | — | HAT SX1262 | HAT SX1262 | por TNC KISS |
| Pantalla táctil (mando local) | — | — | ✔ (vertical) | ✔ (apaisada) | ✔ (7", apaisada) | — | — | — |
| USB como controlador de Hamlib | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | USB gadget (Zero 2 W) |
| CAT de emisora / SDR (CI-V, Yaesu, Kenwood, SDRconnect, rigctl) + rigctld 4532 | ✔ | ✔ | ✔ (UART compartida con la pasarela) | ✔ | ✔ (UART compartida con la pasarela) | ✔ | UART | ✔ (rigctld de Hamlib: todas las emisoras) |
| Red local: viento, posición y rumbo (gpsd, NMEA, Signal K, UDP, OpenWeatherMap) | ✔ | ✔ | ✔ | ✔ | ✔ | — | — | ✔ (`lan.py`) |
| Hora: GPS de la red y NTP `ntp.roa.es` (ROA) | ✔ | ✔ | ✔ | ✔ | ✔ | NTP | — | NTPsec (NTS del ROA, GPS / PPS de gpsd) |
| Actualización por la WebUI | placas de 8 MB | ✔ | ✔ | ✔ | ✔ | ✔ | — | `git pull` + `install.sh` |

## Versión

Todos los repositorios del proyecto llevan **el mismo número de versión**: el rotor es la **5.4.1**, como el tracker APRS LoRa+ y la app Android. Se publican juntos.

## Instalar

**Instalador web** (Chrome o Edge, placa por USB): https://ea7jrs.github.io/APRS-LoRa-Plus/ → elige **Rotor** y tu placa. ESP32, ESP32-S3 y ESP32-C5 se instalan desde el navegador; para la Pico 2 / Pico 2 W descarga el `.uf2` y cópialo a la unidad que aparece al conectarla con BOOTSEL pulsado. El instalador reconoce la placa conectada y su versión (pregunta `APRS-INFO?` por USB).

## Firmware (ESP32, ESP32-S3, ESP32-C5, Pico 2)

PlatformIO (VS Code):

```sh
cd firmware
pio run -e esp32s3 -t upload          # esp32dev · esp32s3 · waveshare_c5_lcd35 · waveshare_s3_amoled241 · waveshare_s3_lcd7b · pico2w · pico2
```

En la Pico 2 se puede copiar `firmware.uf2` arrastrándolo con BOOTSEL pulsado. La CI de GitHub compila todas las placas en cada subida y deja los binarios como artefactos.

Primer arranque: abre el punto de acceso **APRS-Rotor** (contraseña `aprsrotor`), entra en `http://192.168.4.1/` (usuario `admin`, contraseña = PIN `123456`) y en *Configuración* pon tu WiFi, tu indicativo, los pines y el tipo de rotor. Cambia el PIN.

## Raspberry Pi

```sh
cd raspberrypi && sudo ./install.sh
```

Ver [docs/RASPBERRY_PI.md](docs/RASPBERRY_PI.md).

## Documentación

- **Manual de usuario**: [español](docs/MANUAL_ES.md) · [English](docs/MANUAL_EN.md)
- **Guía rápida**: [español](docs/QUICKSTART_ES.md) · [English](docs/QUICKSTART_EN.md)
- [docs/HARDWARE.md](docs/HARDWARE.md): pines, Yaesu G-5500 por el DIN, transistores, divisores, pasarela serie
- [docs/WAVESHARE_C5_LCD35.md](docs/WAVESHARE_C5_LCD35.md): placa con pantalla táctil (conector, relés, potenciómetro, pantalla)
- [docs/WAVESHARE_S3_AMOLED241.md](docs/WAVESHARE_S3_AMOLED241.md): placa con pantalla AMOLED apaisada
- [docs/WAVESHARE_S3_LCD7B.md](docs/WAVESHARE_S3_LCD7B.md): placa con pantalla de 7" (la de APRS LoRa+ Display): relés por PCF8574, potenciómetro en el conector Sensor
- [docs/CAT.md](docs/CAT.md): emisoras (Icom, Yaesu, Kenwood / Elecraft) y receptores SDR (SDRplay RSPdx-R2 con SDRconnect, SDR++, gqrx): conexión, Doppler, CTCSS, PTT, rigctld
- [docs/CONFIG.md](docs/CONFIG.md): todas las opciones de las placas
- [docs/CONTROL_PROTOCOL.md](docs/CONTROL_PROTOCOL.md): protocolo JSON (BLE, TCP, WebUI) para la app
- [docs/APRS_CONTROL.md](docs/APRS_CONTROL.md): mover el rotor con mensajes APRS por LoRa, con código TOTP
- [docs/INTEGRATION.md](docs/INTEGRATION.md): tracker, app, GPredict, Hamlib
- [docs/RASPBERRY_PI.md](docs/RASPBERRY_PI.md): demonio de la Raspberry Pi

## Estructura

```
firmware/                    PlatformIO: ESP32, ESP32-S3, ESP32-C5, RP2350
  lib/RotorCore/src/         núcleo en C++ puro (rotctld, GS-232, EasyComm, Rot2Prog, control, APRS, JSON)
  test_host/                 pruebas del núcleo y de la pantalla táctil en el PC (make run)
  src/                       placa, configuración, mando, red, Bluetooth, LoRa, WebUI
raspberrypi/                 demonio en Python (asyncio, sin dependencias salvo dbus-next para Bluetooth)
docs/
```

Pruebas: `make -C firmware/test_host run ARDUINOJSON=/ruta/ArduinoJson/src` y `cd raspberrypi && python3 -m unittest discover -s tests`.

## Licencia

GPL-3.0-or-later. Copyright (C) 2026 Manuel - EA7JRS.
Formatos de los protocolos tomados del código de [Hamlib](https://github.com/Hamlib/Hamlib) (GS-232A/B, SPID, EasyComm, rotctld).
Pantalla: secuencia del ST7796 y registros del expansor tomados de los ejemplos de Waveshare; letras de DejaVu Sans ([firmware/src/NOTICE_FONTS.md](firmware/src/NOTICE_FONTS.md)).
