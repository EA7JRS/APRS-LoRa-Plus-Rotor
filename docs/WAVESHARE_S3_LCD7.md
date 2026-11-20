# Waveshare ESP32-S3-Touch-LCD-7 (rotor con pantalla de 7", 800 × 480)

Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.

Entorno `waveshare_s3_lcd7`. La **LCD-7 sin B** (rev. 1.2): ESP32-S3-WROOM-1-N16R8 (WiFi 2,4 GHz, BLE 5; sin Bluetooth clásico), 8 MB de PSRAM, 16 MB de flash, pantalla de **7" 800 × 480** (panel RGB de 16 bits, ST7262) con táctil GT911, expansor **CH422G**, microSD, RS485 y CAN. **No lleva LoRa.** Página del producto: https://www.waveshare.com/esp32-s3-touch-lcd-7.htm

No es la misma placa que la [7B](WAVESHARE_S3_LCD7B.md) (1024 × 600, CH32V003): los pines del panel y del táctil son los mismos, pero cambian el expansor, la resolución y los tiempos del panel. Los datos son los de **APRS LoRa+ Display**, que ya funciona en esta placa (entorno `waveshare_lcd7`): 16 MHz de reloj de píxel y 4 / 8 / 8 de sincronía (`waveshare_rgb_lcd_port.c` de Waveshare).

La pantalla se usa **apaisada**, con la disposición de la 7B a escala de 800 × 480 y las letras de la AMOLED de 2,41": brújula a la izquierda, acimut y elevación a la derecha, botones **CCW · STOP · CW · DOWN · PARK · UP** abajo, botón **CAT** en la cabecera (con emisora configurada) y ⓘ para la información, el **Acerca de** y el brillo.

**Brillo**: el CH422G solo enciende o apaga la luz (EXIO2). No hay regulación: cualquier valor de `lcd.bright` por encima de 0 es encendida. Con `lcd.dimS` la pantalla se queda encendida (no baja al 10 % como en la 7B).

## Qué queda libre para el rotor

Los mismos conectores HY2.0 que la 7B:

| Conector | Señales | Uso por defecto |
|---|---|---|
| **Sensor** (3P) | GPIO6 (ADC1), 3V3, GND | **potenciómetro AZ** (`direct.az.adcPin` 6, en milivoltios) |
| **I2C** (4P) | SDA GPIO8, SCL GPIO9, 3V3, GND | ADS1115 opcional (0x48). El bus es el del táctil (0x5D / 0x14) y el CH422G, que contesta en **0x20-0x27 y 0x30-0x3F**: no caben placas de relés con PCF8574 / PCF8574A ni MCP23017 (ver abajo) |
| **UART** (4P) | TX GPIO43, RX GPIO44 | **pasarela serie** al controlador (`driver` 1) **o** CAT de la emisora: la misma UART, una de las dos. Pon el conmutador de la placa en *UART2* (el conector); en la otra posición va al puerto USB *UART* |
| RS485 | RX GPIO16, TX GPIO15 (transceptor con conmutación automática) | **pasarela por RS485**: el bus propio (`serial.proto` 4) o GS-232A/B, EasyComm II y Rot2Prog (`serial.rs485` true), con `serial.rx` 16 / `tx` 15 |
| microSD | SCK 12, MISO 13, MOSI 11, CS en EXIO4 | **registro de movimientos** (`log.sd`, de serie encendido; CSV por día, descarga en `/sd` de la WebUI) |
| CAN | GPIO19 / 20 | no se usa (comparte pines con el USB) |

El USB nativo (Type-C *USB*) hace de puerto serie: instalador web, registro y `usb.proto`. EXIO5 queda **bajo** (USB; alto sería CAN).

## Mando: pasarela de serie

El CH422G no tiene registros: cada función es una dirección I2C, y por eso **ocupa 0x20-0x27 y 0x30-0x3F**. Ahí caen todas las direcciones de un PCF8574 (0x20-0x27), de un PCF8574A (0x38-0x3F) y de un MCP23017: **una placa de relés por I2C no se puede usar en esta placa**, y quedan pocos GPIO libres para relés.

Por eso el `waveshare_s3_lcd7` arranca con **`driver` 1, pasarela serie** a un controlador comercial (GS-232A/B, Rot2Prog, EasyComm II) por el conector UART (`serial.rx` 44 / `tx` 43), o por el **RS485** (`serial.rx` 16 / `tx` 15): el bus propio (`serial.proto` 4) o GS-232A/B, EasyComm II y Rot2Prog con `serial.rs485: true` (una trama cada vez; en el controlador, un convertidor RS485 ↔ RS232). Los pines de los relés vienen vacíos (-1). Si se pone una placa PCF8574 a la fuerza, la configuración la rechaza («direct.pcfAddr: … = CH422G»).

Con la pasarela por el RS485, el conector UART queda libre para el **CAT** de la emisora (`cat.rx` 44 / `tx` 43).

Para control directo con relés, la [LCD-7B](WAVESHARE_S3_LCD7B.md) (su expansor es un CH32V003 en 0x24 y deja sitio para un PCF8574).

## Registro de movimientos (microSD)

Con una tarjeta FAT32 en la ranura, la caja guarda en `/rotor/AAAA-MM-DD.csv` (un fichero por día, UTC) cada orden nueva (`MOVE` con el objetivo), la posición cada `log.everyS` segundos mientras se mueve (`POS`), la llegada o la parada (`STOP`), el mover a mano (`MANUAL`) y los fallos (`FAULT` / `CLEAR`, también el aparcado por viento). Escribe cada 10 s (o antes si se junta mucho), borra lo de hace más de `log.keepDays` días (90) y vuelve a montar la tarjeta sola si se quita y se pone. Lista y descarga: `http://<caja>/sd`. Sin hora (sin NTP ni GPS de la red) va a `nodate.csv` con los segundos desde el arranque.

La tarjeta va por SPI con el CS en el EXIO4 del CH422G; la librería SD pide además un GPIO de CS, que es de relleno (`log.cs`, 6, el del conector *Sensor*, que en esta placa no se usa con la pasarela).

## Pantalla de una Raspberry Pi

La LCD-7 (y la 7B) puede ser la **pantalla de la caja del rotor en una Raspberry Pi** con el Waveshare Stepper Motor HAT, que no tiene pantalla: por **WiFi** (`"driver": 2`, `"remote": {"host": "<IP de la Pi>", "port": 4533}`, cliente de su rotctld) o por **cable** (`driver` 1 GS-232B a 9600 por el conector UART, a los GPIO8 / 9 de la Pi con `install.sh --link-uart`). Detalles en [RASPBERRY_HATS.md](RASPBERRY_HATS.md#emparejar-la-pi-con-una-pantalla-o-con-la-app).

## Sin verificar en la placa

El panel, el táctil y el CH422G están probados con APRS LoRa+ Display en esta placa. Del rotor, sin probar:

1. **Pantalla del rotor** (disposición y letras): probada en el PC (`test_host/lcd_preview`, imágenes de 800 × 480).
2. **Conmutador UART** en la posición del conector y la pasarela serie.
3. **`driver` 2** contra el rotctld de una Raspberry Pi (probado en el PC con el demonio de la Pi).
4. **RS485** con GS-232 / EasyComm / Rot2Prog (`serial.rs485`) y la **microSD** (registro): la cola semidúplex y el registro, probados en el PC (`test_host`).

## Instalar

Instalador web → **Rotor** → `waveshare_s3_lcd7` (por el puerto *USB*), o `pio run -e waveshare_s3_lcd7 -t upload`. 16 MB (`default_16MB.csv`, con actualización por la WebUI), PSRAM octal.
