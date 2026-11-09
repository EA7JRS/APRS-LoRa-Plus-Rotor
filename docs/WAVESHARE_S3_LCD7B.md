# Waveshare ESP32-S3-Touch-LCD-7B (rotor con pantalla de 7")

Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later.

Entorno `waveshare_s3_lcd7b`. La misma placa que **APRS LoRa+ Display**: ESP32-S3-WROOM-1-N16R8 (WiFi 2,4 GHz, BLE 5; sin Bluetooth clásico), 8 MB de PSRAM, 16 MB de flash, pantalla **IPS de 7" 1024 × 600** (panel RGB de 16 bits) con táctil GT911, expansor CH32V003 en 0x24, microSD, RS485, CAN y cargador de batería. **No lleva LoRa.** Página del producto: https://www.waveshare.com/esp32-s3-lcd-7b.htm

La pantalla se usa **apaisada**, con la misma disposición que la [AMOLED de 2,41"](WAVESHARE_S3_AMOLED241.md) a mayor tamaño: brújula a la izquierda, acimut y elevación a la derecha, botones **CCW · STOP · CW · DOWN · PARK · UP** abajo, botón **CAT** en la cabecera (con emisora configurada) y ⓘ para la información, el **Acerca de** y el brillo. Sin uso durante `lcd.dimS` la luz baja al 10 %.

## Qué queda libre para el rotor

Casi todos los GPIO van al panel. Para el rotor quedan los conectores HY2.0:

| Conector | Señales | Uso por defecto |
|---|---|---|
| **Sensor** (3P) | GPIO6 (ADC1), 3V3, GND | **potenciómetro AZ** (`direct.az.adcPin` 6, en milivoltios) |
| **I2C** (4P) | SDA GPIO8, SCL GPIO9, 3V3, GND | **placa de relés PCF8574** (pines 120-127) y ADS1115 opcional para la elevación. El bus es el del táctil (0x5D / 0x14) y el expansor (0x24): no uses esas direcciones |
| **UART** (4P) | TX GPIO43, RX GPIO44 | **pasarela serie** al controlador (`driver` 1) **o** CAT de la emisora: la misma UART, una de las dos. Pon el conmutador *UART* de la placa en el conector |
| RS485 | RX GPIO16, TX GPIO15 (transceptor con conmutación automática) | **pasarela al bus RS485** (`serial.proto` 4, `serial.rx` 16 / `tx` 15): controladores con encóder, uno por eje |
| CAN | GPIO19 / 20 | no se usa (comparte pines con el USB) |

El USB nativo (Type-C *USB*) hace de puerto serie: instalador web, registro y `usb.proto` (la placa se presenta como un GS-232B). EXIO5 queda **bajo** (USB; alto sería CAN): si el puerto USB desaparece al arrancar, compila con `-D EXIO_USB_SEL_LEVEL=1`.

## Control directo con una placa de relés PCF8574

Los relés van por I2C: cualquier placa de 4 u 8 relés con PCF8574 (dirección `direct.pcfAddr`, 0x20 por defecto; **no** 0x24). Los pines 120-127 son P0-P7:

| Pin | PCF8574 | Por defecto |
|---|---|---|
| 120 | P0 | AZ CW |
| 121 | P1 | AZ CCW |
| 122 | P2 | EL UP |
| 123 | P3 | EL DOWN |

Estas placas activan el relé con **0**: por eso `direct.activeHigh` es `false` en esta placa. Si la tuya activa con 1, pon `true`. Solo salidas de nivel (sin PWM): `direct.outMode` 0. El freno (`brakePin`) puede ir en 124.

**Elevación**: con un ADS1115 en el I2C (0x48), `direct.el.sensor` 1 (canal `adsCh` 1) y `direct.elEnabled` true. El acimut también puede ir por el ADS1115 (canal 0) si quieres más resolución que el ADC del ESP32.

**Con el G-5500**: el divisor 10 k / 18 k de [HARDWARE.md](HARDWARE.md) en el conector *Sensor*, y la calibración de dos puntos.

## Pasarela serie o CAT

La UART del conector (3,3 V) sirve para **una** de las dos cosas: un controlador comercial (GS-232A/B, Rot2Prog, EasyComm II: `driver` 1, `serial.rx` 44 / `tx` 43) o la emisora (`cat.proto`, `cat.rx` 44 / `tx` 43). Para RS-232, un MAX3232. Con el rotor por pasarela y la emisora a la vez, la emisora por red: SDRconnect o rigctl ([CAT.md](CAT.md)).

Con la pasarela por el RS485, el conector UART queda libre para el CAT de la emisora.

## Sin verificar en la placa

Tomado de los ejemplos de Waveshare para la 7B (`06_LCD`, `08_TOUCH`, `15_LVGL_SLIDER`); comprobar primero si algo no va:

1. **Expansor** (0x24, registros de 1 byte, EXIOn = bit n): EXIO1 reset del táctil, EXIO2 luz, EXIO3 reset del LCD, EXIO6 VDD del LCD. El PWM de la luz va **al revés** (0 = brillo máximo).
2. **Reloj del panel**: 30 MHz como el ejemplo. Si la imagen tiembla o se desplaza, compila con `-D LCD_PCLK_HZ=21000000`.
3. **Táctil**: misma orientación que el panel (sin girar ejes); dirección 0x5D o 0x14, la que conteste.
4. **EXIO5** (USB / CAN): la documentación de Waveshare dice bajo = USB.

## Instalar

Instalador web → **Rotor** → `waveshare_s3_lcd7b` (por el puerto *USB*, no el *UART*), o `pio run -e waveshare_s3_lcd7b -t upload`. 16 MB (`default_16MB.csv`, con actualización por la WebUI), PSRAM octal.
