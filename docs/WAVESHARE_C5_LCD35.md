# Waveshare ESP32-C5-Touch-LCD-3.5 (rotor con pantalla táctil)

Entorno `waveshare_c5_lcd35`. Placa de Waveshare con ESP32-C5 (WiFi 2,4 y 5 GHz, BLE 5; **sin Bluetooth clásico**: no hay SPP GS-232 para el tracker con ESP32 original, que puede usar la WiFi), pantalla IPS táctil de 3,5" (320 × 480, ST7796 + FT6336), 8 MB de PSRAM, 32 MB de flash, AXP2101, IMU, RTC, SHTC3, audio y microSD. **No lleva LoRa.** Documentación del fabricante: https://docs.waveshare.com/ESP32-C5-Touch-LCD-3.5

La caja del rotor se maneja en la propia pantalla, además de por la app, la WebUI, rotctld y el USB.

## Pantalla

- **Mando**: brújula con la posición (aguja roja), el objetivo (azul) y la zona de solape (arco naranja si `az.max` > 360), lecturas de acimut y elevación, objetivo / movimiento / fallo, y botones **CCW · STOP · CW · DOWN · PARK · UP**. CCW, CW, UP y DOWN mueven mientras se mantienen pulsados; STOP para; PARK va a la posición de reposo.
- **Apuntar**: mantén pulsada la brújula medio segundo, arrastra hasta el rumbo (aguja verde) y suelta: el rotor va a ese acimut (la elevación se queda como está). Un toque corto no hace nada, para no mover el rotor sin querer.
- **Fallo** (atasco, sensor…): aparece en rojo; tócalo para borrarlo.
- **Información** (botón ⓘ de la cabecera): nombre, indicativo, placa, chip, mando, WiFi, IP, puertos, Bluetooth, tiempo encendida, **Acerca de** (versión, © 2026 Manuel - EA7JRS, licencia) y brillo.
- Brillo y apagado: `lcd.bright` y `lcd.dimS` en la configuración (también desde la app y la WebUI). El primer toque con la pantalla atenuada solo la enciende.

## Conector de 32 pines (J3)

| Pin | Señal | Uso en el rotor | | Pin | Señal | Uso en el rotor |
|---|---|---|---|---|---|---|
| 1 | VBUS (5 V del USB) | | | 2 | VBAT | |
| 3 | GND | | | 4 | GND | |
| 5 | SWDIO (programación del CH32V) | no tocar | | 6 | EXIO8 | **AZ CW** (pin 108) |
| 7 | — | | | 8 | EXIO9 | **AZ CCW** (109) |
| 9 | — | | | 10 | EXIO10 | **EL UP** (110) |
| 11 | NC/GPIO22 (no conectado en el C5) | | | 12 | EXIO11 | **EL DOWN** (111) |
| 13 | NC/GPIO21 (no conectado en el C5) | | | 14 | EXIO12 | libre (freno: `brakePin` 112) |
| 15 | GPIO0_OUT | | | 16 | EXIO13 | libre |
| 17 | GPIO1_OUT | | | 18 | EXIO14 | libre |
| 19 | GPIO9_OUT | | | 20 | EXIO15 | libre |
| 21 | GPIO28_OUT (botón BOOT) | | | 22 | CHIP_PU | |
| 23 | EXIO_ADC | **potenciómetro AZ** (pin 200) | | 24 | PWRON | |
| 25 | ESP32_SCL (GPIO26) | **I2C del ADS1115** | | 26 | U0TXD (GPIO11) | **TX de la pasarela serie** |
| 27 | ESP32_SDA (GPIO27) | **I2C del ADS1115** | | 28 | U0RXD (GPIO12) | **RX de la pasarela serie** |
| 29 | GND | | | 30 | GND | |
| 31 | 3V3 | | | 32 | 3V3 | |

Números de pin según el esquema de Waveshare (`hardware/schematic` de su repositorio); compruébalos con el dibujo de la placa antes de soldar. GPIO0_OUT, GPIO1_OUT, GPIO9_OUT, U0TXD y U0RXD pasan por resistencias de 0 Ω de tres patas que eligen entre la cámara / microSD y el conector: si la pasarela serie no responde, revisa en qué posición están R56 y R59.

**Salidas EXIO**: son del expansor CH32V (I2C 0x24), a 3,3 V y con muy poca corriente. Siempre a través de un módulo de relés con transistor u optoacoplador (o un ULN2003). Son de nivel: con la placa no hay puente H por PWM (`direct.outMode` 0).

**EXIO_ADC**: entrada analógica del expansor (0-3,3 V); la lectura va en cuentas. Con el G-5500, el mismo divisor 10 k / 18 k de [HARDWARE.md](HARDWARE.md); calibra con «Punto A / B» (WebUI o app).

**Elevación**: la placa solo tiene una entrada analógica libre. Para un rotor AZ/EL conecta un **ADS1115** al I2C del conector (SDA 27, SCL 26, 3V3, GND; dirección 0x48, comparte el bus con el táctil, el RTC, el IMU y el PMU) y en la configuración pon `direct.elEnabled: true`, `direct.el.sensor: 1`, `direct.el.adsCh: 1` (y, si quieres, el acimut también: `direct.az.sensor: 1`, `adsCh: 0`). Por defecto la elevación está apagada.

## Instalar

Instalador web (ESP Web Tools 10.4, Chrome o Edge) → **Rotor** → `waveshare_c5_lcd35`. O PlatformIO: `pio run -e waveshare_c5_lcd35 -t upload`. El entorno usa la plataforma **pioarduino** (Arduino 3.3.12 con soporte del ESP32-C5; la espressif32 oficial no lo trae) y 16 MB de los 32 de flash (tabla `default_16MB.csv`, con actualización por la WebUI).

## Estado

Compila en la CI; la pantalla se ha probado en el PC (`firmware/test_host/lcd_preview`, dibuja las pantallas en `lcd_out/`). **Sin probar en la placa**: comprobar la orientación del táctil, el brillo y el formato de 16 bits del expansor (los ejemplos de Waveshare traen dos variantes: CH32V006 con 16 pines y CH32V003 con 8).
