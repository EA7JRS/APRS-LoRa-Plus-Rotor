# APRS LoRa+ Rotor — contexto del proyecto

Propietario: Manuel (EA7JRS). Idioma de trabajo: español, respuestas breves y directas.
Archivos nuevos con la cabecera `Copyright (C) 2026 Manuel - EA7JRS` y licencia GPL-3.0-or-later.
Repositorio: `EA7JRS/APRS-LoRa-Plus-Rotor` (privado, creado por Manuel el 2026-11-07). Proyectos hermanos: tracker `EA7JRS/APRS-LoRa-Plus` (firmware ESP32, fork de CA2RXU) y app `EA7JRS/APRS-LoRa-Plus-Android`.

## Qué es
Controlador de rotores compatible con Hamlib, manejado por el tracker APRS LoRa+, la app Android y cualquier programa con Hamlib.

## Decisiones (Manuel, 2026-11-07)
- Placas: **ESP32 y ESP32-S3**, **Raspberry Pi Pico 2 / Pico 2 W** (RP2350, pedido después), **Raspberry Pi** (Zero 2 W, 3, 4, 5) **Waveshare ESP32-C5-Touch-LCD-3.5** (pedida 2026-11-08; Manuel eligió «rotor con pantalla») y **Waveshare ESP32-S3-Touch-AMOLED-2.41** (V2, pedida 2026-11-08, mismo uso).
- ESP32 / Pico: **control directo** (relés / puente H + potenciómetro por ADC o ADS1115) **y pasarela serie** a un controlador comercial (GS-232A/B, Rot2Prog, EasyComm II).
- Raspberry Pi: **rotctld real de Hamlib + demonio Python** (cualquier modelo de Hamlib).
- Enlaces: **rotctld TCP por WiFi**, **BLE propio** (protocolo JSON, para la app), **Bluetooth SPP GS-232** (tracker con ESP32 original) y **rotor por LoRa/APRS** (mensajes `P az el` con lista de indicativos y TOTP opcional).
- Regla del ecosistema: todo lo del rotor debe poder controlarse desde la app Android.

## Estructura
- `firmware/lib/RotorCore/src/`: núcleo C++ puro, sin Arduino (`rc_rotctld.h`, `rc_serialproto.h`, `rc_control.h`, `rc_aprs.h`, `rc_api.h`). Pruebas: `firmware/test_host` (`make run ARDUINOJSON=…/ArduinoJson/src`; 168 comprobaciones).
- `firmware/src/`: `board` (capa ESP32 / RP2350), `config` (LittleFS `/rotor_conf.json`), `drivers` (directo + pasarela), `services` (puerto serie con protocolo, anfitrión JSON), `net` (WiFi, rotctld 4533, JSON 4534, WebUI 80, OTA `/update`, mDNS), `bt_ble` (NimBLE), `bt_spp`, `lora_aprs` (RadioLib, emisión sin bloquear), `web_page.h` (la WebUI; la Raspberry la usa también).
- Entornos: `esp32dev`, `esp32s3`, `waveshare_c5_lcd35`, `waveshare_s3_amoled241`, `pico2w`, `pico2`. Versiones como el tracker: espressif32 8.0.0, NimBLE 2.5.2, RadioLib 7.8.1, ArduinoJson 7.4.3; RP2350 con maxgerhardt/platform-raspberrypi (arduino-pico). `-Werror` solo en `src` (`build_src_flags`).
- ESP32 de 4 MB: partición `huge_app.csv` (BLE + SPP + WiFi no caben con OTA): sin OTA por la WebUI.
- `raspberrypi/aprs_lora_plus_rotor/`: demonio asyncio (stdlib + `python3-dbus-next` para BLE/SPP): `hamlib.py` (arranca y vigila `rotctld` en 127.0.0.1:4535 y es su cliente), `protocols.py`, `api.py`, `aprs.py`, `kiss.py`, `bluez.py`, `web.py`, `serialport.py` (termios), `main.py`. Pruebas: `python3 -m unittest discover -s tests` (incluye un rotctld simulado).
- CI: `.github/workflows/build.yml` (pruebas del núcleo, pruebas Python y las 6 placas; binarios como artefactos). TTGO LoRa32 V2.1 y Heltec V3 eliminadas por Manuel (2026-11-08); el LoRa APRS sigue con módulo externo en ESP32 / ESP32-S3 y con el HAT SX1262 en la Pico.

## Waveshare ESP32-C5-Touch-LCD-3.5 (`waveshare_c5_lcd35`, 2026-11-08)
- Plataforma **pioarduino** 55.03.312-1 (Arduino 3.3.12 como espressif32 8.0.0, que no trae el C5), placa `esp32-c5-devkitc1-n8r4` con 16 MB (`default_16MB.csv`). Sin Bluetooth clásico (no SPP) ni LoRa.
- Expansor CH32V en I2C 0x24 (`exio.cpp`; SDA 27, SCL 26, compartido con FT6336, ADS1115, RTC, IMU, PMU): EXIO0 reset táctil, EXIO1 reset LCD, EXIO5 amplificador, registro 0x05 = brillo. Pines virtuales: **100-115 = EXIOn**, **200 = EXIO_ADC** (`Board::pinOutput`, `digitalOut`, `readAdc`). Por defecto relés en EXIO8-11 (conector J3 pines 6-12), potenciómetro AZ en EXIO_ADC, elevación apagada (necesita ADS1115), pasarela por U0TXD/U0RXD (GPIO11/12, `Serial1`: el C5 tiene dos UART).
- Pantalla (`lcd_gfx.h`, `lcd_ui.h` sin Arduino; `lcd_hw.cpp` con ST7796 por SPI 40 MHz, MOSI 7, SCLK 6, CS 8, DC 5, CS de la microSD 9 arriba): dos imágenes de 320×480 en PSRAM, solo se mandan las franjas de 16 filas que cambian (3 por vuelta del bucle). Mando (brújula, AZ/EL, CCW/STOP/CW/DOWN/PARK/UP, apuntar manteniendo la brújula 0,5 s), información y Acerca de. `lcd.bright`, `lcd.dimS`; `caps.lcd`.
- Fuentes (DejaVu, `NOTICE_FONTS.md`) y logo generados por `tools/make_lcd_assets.py` (necesita rsvg-convert). Prueba en PC: `test_host/lcd_preview` (24 comprobaciones; PPM en `lcd_out/`).
- Instalador web: ESP32-C5 necesita ESP Web Tools ≥ 10.2 (el tracker pasa a 10.4.0 en el PR #3); cargador en 0x2000.
- Sin probar en la placa (orientación del táctil, formato de 16 bits del expansor: Waveshare tiene ejemplos con CH32V006 de 16 pines y CH32V003 de 8).

## Waveshare ESP32-S3-Touch-AMOLED-2.41 V2 (`waveshare_s3_amoled241`, 2026-11-08)
- espressif32 8.0.0, `esp32-s3-devkitc-1` con PSRAM octal y 16 MB. Fuente de los pines: repositorio `waveshareteam/ESP32-S3-Touch-AMOLED-2.41-V2` (esquema V2.0, `09_LVGL_Test`).
- AMOLED RM690B0 por QSPI (`LCD_PANEL 2`: CS 9, CLK 10, D0-D3 11-14; `esp_lcd_panel_io` con `quad_mode`, órdenes `0x02<<24 | cmd<<8`, puntos `0x32<<24 | 0x2C<<8`), girada 90° (MADCTL 0x30), 600×450 con 16 filas de desplazamiento; ventanas en múltiplos de 2. Brillo con 0x51; sin uso se apaga (`LCD_DIM_PCT` 0). Franjas copiadas a un búfer DMA interno.
- Táctil FT6336 en I2C 47/48: X = 599 − Yraw, Y = Xraw. TCA9554 0x20 (`EXIO_CHIP 2`): EXIO0 reset de la pantalla, EXIO1 reset del táctil, EXIO5-7 en el conector. IO16 arriba (`BOARD_POWER_HOLD`) para seguir encendida con batería.
- Por defecto: relés IO38-41, potenciómetros IO1 / IO7 (ADC1), pasarela RX 44 / TX 43.
- `lcd_ui.h` con dos disposiciones (`Layout::make`: vertical 320×480 y apaisada 600×450, fuentes `FontS2/M2/L2` de 22/28/54 px). `lcd_preview` prueba las dos (72 comprobaciones).

## Protocolos
- BLE: servicio `4c6f5261-2b00-4ea7-b000-000000000001`, órdenes `…0002`, eventos `…0003` (el tracker usa `…-a000-…`). JSON por líneas, PIN con `auth`, emparejamiento Just Works cifrado. Documento: `docs/CONTROL_PROTOCOL.md`.
- rotctld: `\dump_state` con protocolo 1 (`clave=valor` y `done`), como Hamlib 4.x. El tracker lo corrige en su PR #3.
- APRS: destino `APZLRR` (experimental), `docs/APRS_CONTROL.md`.
- G-5500: DIN pin 6 = AZ 2-4,5 V (0-450°), pin 1 = EL 2-4,5 V (0-180°), 2 CW, 4 CCW, 3 UP, 5 DOWN, 8 masa (manual de Yaesu). Divisor 10 k / 18 k.

## Estado (2026-11-08)
- Núcleo probado en el PC; demonio de la Raspberry probado en el PC con un rotctld simulado y un TNC KISS simulado.
- Firmware: compila en la CI; **sin probar en placas**. BLE en la Pico 2 W: pendiente (arduino-pico usa BTstack, otro API).
- App Android: pestaña **Rotor** hecha (commit 8655fab de APRS-LoRa-Plus-Android, sin probar con una caja real).
- Tracker: PR #3 (rama `rotor-follow`): `\dump_state` corregido, fuentes 4 (última estación oída) y 5 (baliza CW con locator) y estimación de estaciones en movimiento; app con lo mismo (a4fa810). Transporte LoRa APRS desde el tracker descartado (su radio escucha lo que sigue).

## Versión y publicación (Manuel, 2026-11-07)
- **Mismo número de versión en todos los repositorios** (tracker, rotor, app, gateway): ahora **5.3.5**. `ROTOR_VERSION` en `firmware/src/config.h`, `__version__` en `raspberrypi/aprs_lora_plus_rotor/__init__.py`, `library.json`.
- WebUI: cabecera como la del tracker (barra roja, logo, «APRS LoRa+» y debajo «Rotor» + versión).
- Instalador web: el del tracker (GitHub Pages de APRS-LoRa-Plus) con tres lanzadores: Tracker, Rotor, Gateway. Release del rotor (`.github/workflows/release.yml`, etiqueta `v` + versión) → `rotor-<placa>.zip` y `web-site-rotor.zip` (`tools/make_web_assets.py`: manifiestos de ESP Web Tools, UF2 de la Pico, `latest.json`, `ota/`); la release del tracker con la misma versión lo descarga (secreto `FLASHER_TOKEN` en APRS-LoRa-Plus porque este repositorio es privado) y lo pone en `rotor/`. Por USB la placa contesta a `APRS-INFO?` con `APRS-LoRa-Plus: device=rotor board=… version=… chip=… ota=…`.

## Cómo trabajar
- No publicar releases ni tocar otros repositorios sin que lo pida Manuel.
- Antes de subir: pruebas del núcleo y de Python; la CI compila las placas.
