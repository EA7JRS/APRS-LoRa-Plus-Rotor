# Waveshare ESP32-S3-Touch-AMOLED-2.41 (rotor con pantalla AMOLED)

Entorno `waveshare_s3_amoled241`. Placa de Waveshare (versión **V2**, la del repositorio `ESP32-S3-Touch-AMOLED-2.41-V2` del fabricante) con ESP32-S3R8 (WiFi 2,4 GHz, BLE 5; sin Bluetooth clásico), 8 MB de PSRAM, 16 MB de flash, pantalla **AMOLED de 2,41" 600 × 450** (RM690B0 por QSPI) con táctil FT6336, expansor TCA9554, RTC PCF85063, IMU QMI8658, microSD y cargador de batería. **No lleva LoRa.** Página del producto: https://www.waveshare.com/esp32-s3-touch-amoled-2.41.htm

La pantalla se usa **apaisada**: brújula a la izquierda, acimut y elevación a la derecha y la fila de botones **CCW · STOP · CW · DOWN · PARK · UP** abajo. Funciona igual que la de la [ESP32-C5-Touch-LCD-3.5](WAVESHARE_C5_LCD35.md): mover manteniendo pulsado, apuntar manteniendo la brújula medio segundo y soltando, tocar el fallo para borrarlo, botón ⓘ para la información, el **Acerca de** y el brillo.

**AMOLED**: los puntos que están siempre encendidos se gastan y dejan marca. Por eso, pasado `lcd.dimS` (300 s por defecto) sin tocarla, la pantalla se **apaga** (no se atenúa como la LCD); el primer toque la enciende. Si la caja va a estar siempre a la vista, baja `lcd.dimS` y el brillo.

## Conector de 34 pines (J8)

| Pin | Señal | Uso en el rotor | | Pin | Señal | Uso en el rotor |
|---|---|---|---|---|---|---|
| 1 | GND | | | 2 | BAT | |
| 3 | IO1 | **potenciómetro AZ** (ADC1) | | 4 | GND | |
| 5 | IO2 (CS de la microSD) | | | 6 | EXIO5 | libre (pin 105) |
| 7 | IO4 (microSD) | | | 8 | EXIO6 | libre (106) |
| 9 | IO5 (microSD) | | | 10 | EXIO7 | libre (107) |
| 11 | IO6 (microSD) | | | 12 | TXD (IO43) | **TX de la pasarela serie** |
| 13 | IO7 | **potenciómetro EL** (ADC1) | | 14 | RXD (IO44) | **RX de la pasarela serie** |
| 15 | IO8 | libre (ADC1) | | 16 | 3V3 | |
| 17 | IO18 (interrupción del TCA9554) | no usar | | 18 | GND | |
| 19 | IO38 | **AZ CW** | | 20 | SDA (IO47) | I2C (ADS1115 opcional) |
| 21 | IO39 | **AZ CCW** | | 22 | SCL (IO48) | I2C (ADS1115 opcional) |
| 23 | IO40 | **EL UP** | | 24 | IO42 | libre (freno: `brakePin` 42) |
| 25 | IO41 | **EL DOWN** | | 26 | IO45 | (pin de arranque: mejor no) |
| 27 | USB D+ (IO20) | | | 28 | IO46 | (pin de arranque: mejor no) |
| 29 | USB D- (IO19) | | | 30 | 3V3 | |
| 31 | USB 5 V | | | 32 | 3V3 | |
| 33 | GND | | | 34 | GND | |

Según el esquema `ESP32-S3-Touch-AMOLED-2.41 V2.0.pdf` de Waveshare; compruébalo con la serigrafía de tu placa antes de soldar.

- **Salidas** (IO38-IO41): 3,3 V y poca corriente, siempre con un módulo de relés con transistor u optoacoplador (o un ULN2003). Aquí sí hay PWM: vale también un puente H (`direct.outMode` 1).
- **Potenciómetros** (IO1, IO7): ADC1, entrada 0-3,3 V, lectura en milivoltios. Con el G-5500, el divisor 10 k / 18 k de [HARDWARE.md](HARDWARE.md) y la calibración de dos puntos. Para más resolución, un ADS1115 en SDA / SCL.
- **EXIO5-EXIO7**: salidas del TCA9554 (pines 105-107), solo de nivel.
- **IO16** se pone arriba al arrancar: con batería, es lo que mantiene la placa encendida.

## Instalar

Instalador web → **Rotor** → `waveshare_s3_amoled241`, o `pio run -e waveshare_s3_amoled241 -t upload`. 16 MB (`default_16MB.csv`, con actualización por la WebUI), PSRAM octal.

## Estado

Compila en la CI; la pantalla apaisada se ha probado en el PC (`firmware/test_host/lcd_preview`, ficheros `lcd_out/600x450_*`). **Sin probar en la placa**: comprobar el giro del táctil (se toma como en el ejemplo de Waveshare: espejo de Y y ejes cambiados), el desplazamiento de 16 filas de la memoria del panel y el brillo. Si tu placa es la primera versión (sin «V2»), los pines pueden ser otros.
