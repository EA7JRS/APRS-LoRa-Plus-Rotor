# APRS LoRa+ Rotor

Controlador de rotores de antena **compatible con Hamlib** para el ecosistema **APRS LoRa+**: lo manejan el [tracker APRS LoRa+](https://github.com/EA7JRS/APRS-LoRa-Plus), la app Android APRS LoRa+ y cualquier programa con Hamlib (GPredict, Look4Sat, PstRotator…).

**Placas**: ESP32, ESP32-S3 (incluidas TTGO LoRa32 V2.1 y Heltec V3 con LoRa), Raspberry Pi Pico 2 / Pico 2 W y Raspberry Pi (Zero 2 W, 3, 4, 5).

```
                       ┌──────────────── APRS LoRa+ Rotor ────────────────┐
 Tracker APRS LoRa+ ──►│ rotctld (WiFi 4533)   · GS-232B por SPP           │
                       │ mensajes APRS por LoRa (P az el + TOTP)           │──► rotor
 App Android ─────────►│ BLE / TCP 4534: protocolo JSON (PIN)              │    · control directo (relés / puente H + potenciómetro)
 GPredict, Look4Sat ──►│ rotctld (Hamlib NET rotctl, modelo 2)             │    · pasarela serie (GS-232, Rot2Prog, EasyComm)
 PC con Hamlib (USB) ─►│ se hace pasar por GS-232A/B, EasyComm II, Rot2Prog│    · Raspberry: rotctld de Hamlib (cualquier modelo)
 Navegador ───────────►│ WebUI (brújula, mando, calibración, config, OTA)  │
                       └───────────────────────────────────────────────────┘
```

## Qué hace cada placa

| | ESP32 | ESP32-S3 | Pico 2 W | Pico 2 | Raspberry Pi |
|---|---|---|---|---|---|
| Control directo (relés / puente H, ADC / ADS1115) | ✔ | ✔ | ✔ | ✔ | — |
| Pasarela serie a un controlador | ✔ | ✔ | ✔ | ✔ | ✔ (rotctld de Hamlib: todos los modelos) |
| rotctld por WiFi, JSON por TCP, WebUI | ✔ | ✔ | ✔ | — | ✔ |
| BLE para la app | ✔ | ✔ | pendiente | — | ✔ |
| SPP (GS-232) para el tracker | ✔ | — | ✔ | — | ✔ |
| LoRa APRS | con módulo (LoRa32 V2.1 integrado) | con módulo (Heltec V3 integrado) | HAT SX1262 | HAT SX1262 | por TNC KISS |
| USB como controlador de Hamlib | ✔ | ✔ | ✔ | ✔ | USB gadget (Zero 2 W) |
| Actualización por la WebUI | placas de 8 MB | ✔ | ✔ | — | `git pull` + `install.sh` |

## Firmware (ESP32, ESP32-S3, Pico 2)

PlatformIO (VS Code):

```sh
cd firmware
pio run -e esp32s3 -t upload          # esp32dev · esp32s3 · ttgo-lora32-v21 · heltec_v3 · pico2w · pico2
```

En la Pico 2 se puede copiar `firmware.uf2` arrastrándolo con BOOTSEL pulsado. La CI de GitHub compila todas las placas en cada subida y deja los binarios como artefactos.

Primer arranque: abre el punto de acceso **APRS-Rotor** (contraseña `aprsrotor`), entra en `http://192.168.4.1/` (usuario `admin`, contraseña = PIN `123456`) y en *Configuración* pon tu WiFi, tu indicativo, los pines y el tipo de rotor. Cambia el PIN.

## Raspberry Pi

```sh
cd raspberrypi && sudo ./install.sh
```

Ver [docs/RASPBERRY_PI.md](docs/RASPBERRY_PI.md).

## Documentación

- [docs/HARDWARE.md](docs/HARDWARE.md): pines, Yaesu G-5500 por el DIN, transistores, divisores, pasarela serie
- [docs/CONFIG.md](docs/CONFIG.md): todas las opciones de las placas
- [docs/CONTROL_PROTOCOL.md](docs/CONTROL_PROTOCOL.md): protocolo JSON (BLE, TCP, WebUI) para la app
- [docs/APRS_CONTROL.md](docs/APRS_CONTROL.md): mover el rotor con mensajes APRS por LoRa, con código TOTP
- [docs/INTEGRATION.md](docs/INTEGRATION.md): tracker, app, GPredict, Hamlib
- [docs/RASPBERRY_PI.md](docs/RASPBERRY_PI.md): demonio de la Raspberry Pi

## Estructura

```
firmware/                    PlatformIO: ESP32, ESP32-S3, RP2350
  lib/RotorCore/src/         núcleo en C++ puro (rotctld, GS-232, EasyComm, Rot2Prog, control, APRS, JSON)
  test_host/                 pruebas del núcleo en el PC (make run)
  src/                       placa, configuración, mando, red, Bluetooth, LoRa, WebUI
raspberrypi/                 demonio en Python (asyncio, sin dependencias salvo dbus-next para Bluetooth)
docs/
```

Pruebas: `make -C firmware/test_host run ARDUINOJSON=/ruta/ArduinoJson/src` y `cd raspberrypi && python3 -m unittest discover -s tests`.

## Licencia

GPL-3.0-or-later. Copyright (C) 2026 Manuel - EA7JRS.
Formatos de los protocolos tomados del código de [Hamlib](https://github.com/Hamlib/Hamlib) (GS-232A/B, SPID, EasyComm, rotctld).
