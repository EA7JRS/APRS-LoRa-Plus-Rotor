# APRS LoRa+ Rotor — contexto del proyecto

Propietario: Manuel (EA7JRS). Idioma de trabajo: español, respuestas breves y directas.
Archivos nuevos con la cabecera `Copyright (C) 2026 Manuel - EA7JRS` y licencia GPL-3.0-or-later.
Repositorio: `EA7JRS/APRS-LoRa-Plus-Rotor` (privado, creado por Manuel el 2026-11-07). Proyectos hermanos: tracker `EA7JRS/APRS-LoRa-Plus` (firmware ESP32, fork de CA2RXU) y app `EA7JRS/APRS-LoRa-Plus-Android`.

## Qué es
Controlador de rotores compatible con Hamlib, manejado por el tracker APRS LoRa+, la app Android y cualquier programa con Hamlib.

## Decisiones (Manuel, 2026-11-07)
- Placas: **ESP32 y ESP32-S3**, **Raspberry Pi Pico 2 / Pico 2 W** (RP2350, pedido después) y **Raspberry Pi** (Zero 2 W, 3, 4, 5).
- ESP32 / Pico: **control directo** (relés / puente H + potenciómetro por ADC o ADS1115) **y pasarela serie** a un controlador comercial (GS-232A/B, Rot2Prog, EasyComm II).
- Raspberry Pi: **rotctld real de Hamlib + demonio Python** (cualquier modelo de Hamlib).
- Enlaces: **rotctld TCP por WiFi**, **BLE propio** (protocolo JSON, para la app), **Bluetooth SPP GS-232** (tracker con ESP32 original) y **rotor por LoRa/APRS** (mensajes `P az el` con lista de indicativos y TOTP opcional).
- Regla del ecosistema: todo lo del rotor debe poder controlarse desde la app Android.

## Estructura
- `firmware/lib/RotorCore/src/`: núcleo C++ puro, sin Arduino (`rc_rotctld.h`, `rc_serialproto.h`, `rc_control.h`, `rc_aprs.h`, `rc_api.h`). Pruebas: `firmware/test_host` (`make run ARDUINOJSON=…/ArduinoJson/src`; 168 comprobaciones).
- `firmware/src/`: `board` (capa ESP32 / RP2350), `config` (LittleFS `/rotor_conf.json`), `drivers` (directo + pasarela), `services` (puerto serie con protocolo, anfitrión JSON), `net` (WiFi, rotctld 4533, JSON 4534, WebUI 80, OTA `/update`, mDNS), `bt_ble` (NimBLE), `bt_spp`, `lora_aprs` (RadioLib, emisión sin bloquear), `web_page.h` (la WebUI; la Raspberry la usa también).
- Entornos: `esp32dev`, `esp32s3`, `ttgo-lora32-v21`, `heltec_v3`, `pico2w`, `pico2`. Versiones como el tracker: espressif32 8.0.0, NimBLE 2.5.2, RadioLib 7.8.1, ArduinoJson 7.4.3; RP2350 con maxgerhardt/platform-raspberrypi (arduino-pico). `-Werror` solo en `src` (`build_src_flags`).
- ESP32 de 4 MB: partición `huge_app.csv` (BLE + SPP + WiFi no caben con OTA): sin OTA por la WebUI.
- `raspberrypi/aprs_lora_plus_rotor/`: demonio asyncio (stdlib + `python3-dbus-next` para BLE/SPP): `hamlib.py` (arranca y vigila `rotctld` en 127.0.0.1:4535 y es su cliente), `protocols.py`, `api.py`, `aprs.py`, `kiss.py`, `bluez.py`, `web.py`, `serialport.py` (termios), `main.py`. Pruebas: `python3 -m unittest discover -s tests` (incluye un rotctld simulado).
- CI: `.github/workflows/build.yml` (pruebas del núcleo, pruebas Python y las 6 placas; binarios como artefactos).

## Protocolos
- BLE: servicio `4c6f5261-2b00-4ea7-b000-000000000001`, órdenes `…0002`, eventos `…0003` (el tracker usa `…-a000-…`). JSON por líneas, PIN con `auth`, emparejamiento Just Works cifrado. Documento: `docs/CONTROL_PROTOCOL.md`.
- rotctld: `\dump_state` con protocolo 1 (`clave=valor` y `done`), como Hamlib 4.x. El tracker lo corrige en su PR #3.
- APRS: destino `APZLRR` (experimental), `docs/APRS_CONTROL.md`.
- G-5500: DIN pin 6 = AZ 2-4,5 V (0-450°), pin 1 = EL 2-4,5 V (0-180°), 2 CW, 4 CCW, 3 UP, 5 DOWN, 8 masa (manual de Yaesu). Divisor 10 k / 18 k.

## Estado (2026-11-07)
- Núcleo probado en el PC; demonio de la Raspberry probado en el PC con un rotctld simulado y un TNC KISS simulado.
- Firmware: compila en la CI; **sin probar en placas**. BLE en la Pico 2 W: pendiente (arduino-pico usa BTstack, otro API).
- App Android: pestaña **Rotor** hecha (commit 8655fab de APRS-LoRa-Plus-Android, sin probar con una caja real).
- Tracker: PR #3 (rama `rotor-follow`): `\dump_state` corregido, fuentes 4 (última estación oída) y 5 (baliza CW con locator) y estimación de estaciones en movimiento; app con lo mismo (a4fa810). Transporte LoRa APRS desde el tracker descartado (su radio escucha lo que sigue).

## Cómo trabajar
- No publicar releases ni tocar otros repositorios sin que lo pida Manuel.
- Antes de subir: pruebas del núcleo y de Python; la CI compila las placas.
