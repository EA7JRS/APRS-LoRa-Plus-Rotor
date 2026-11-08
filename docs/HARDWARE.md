# Hardware y conexiones

Dos maneras de mover el rotor (campo `driver` de la configuración):

- **Control directo** (`driver: 0`): la placa sustituye al ordenador del mando. Activa los relés / transistores de giro y lee la posición del potenciómetro.
- **Pasarela serie** (`driver: 1`): la placa habla por UART (TTL o RS-232 con un MAX3232) con un controlador comercial: Yaesu GS-232A/B, SPID Rot2Prog / MD-01 / MD-02, o EasyComm II (K3NG, SatNOGS, ERC…).

> ⚠️ Las placas trabajan a **3,3 V**. Ninguna entrada puede recibir más: usa divisores de tensión y transistores u optoacopladores. Las líneas del mando y de los motores nunca van directas a un pin.

## Pines por defecto

Todos se cambian en la configuración (WebUI o app). `-1` = sin usar.

| Placa (entorno) | AZ CW | AZ CCW | EL UP | EL DOWN | ADC AZ | ADC EL | I2C SDA/SCL (ADS1115) | UART pasarela RX/TX | LoRa |
|---|---|---|---|---|---|---|---|---|---|
| ESP32 DevKit (`esp32dev`) | 25 | 26 | 27 | 14 | 34 | 35 | 21 / 22 | 16 / 17 | — |
| ESP32-S3 DevKitC-1 (`esp32s3`) | 4 | 5 | 6 | 7 | 1 | 2 | 8 / 9 | 18 / 17 | — |
| Waveshare ESP32-C5-Touch-LCD-3.5 (`waveshare_c5_lcd35`) | EXIO8 (108) | EXIO9 (109) | EXIO10 (110) | EXIO11 (111) | EXIO_ADC (200) | ADS1115 canal 1 | 27 / 26 | GPIO12 / GPIO11 (U0RXD / U0TXD) | — |
| Waveshare ESP32-S3-Touch-AMOLED-2.41 (`waveshare_s3_amoled241`) | 38 | 39 | 40 | 41 | 1 | 7 | 47 / 48 | 44 / 43 | — |
| Pico 2 W (`pico2w`) / Pico 2 (`pico2`) | GP6 | GP7 | GP8 | GP9 | GP26 | GP27 | GP4 / GP5 | GP1 / GP0 | HAT SX1262 Waveshare (opcional) |

ADC: en ESP32 usa solo pines del ADC1 (el ADC2 no funciona con la WiFi encendida). En la Pico, GP26-GP28. Placas con pantalla: [WAVESHARE_C5_LCD35.md](WAVESHARE_C5_LCD35.md) y [WAVESHARE_S3_AMOLED241.md](WAVESHARE_S3_AMOLED241.md).

CAT de la emisora (`cat.rx` / `cat.tx`): ESP32 32 / 33, ESP32-S3 15 / 16, ESP32-C5 LCD 12 / 11 (la misma UART que la pasarela: una u otra), ESP32-S3 AMOLED 8 / 42, Pico a elegir. Conexiones y niveles en [CAT.md](CAT.md).

LoRa en la Pico (HAT «Pico-LoRa-SX1262» de Waveshare): SCK GP10, MOSI GP11, MISO GP12, CS GP3, RESET GP15, BUSY GP2, DIO1 GP20. Elige el chip en `lora.chip` (1 SX1262, 2 SX1268, 3 SX1276, 4 SX1278).

## Yaesu G-5500 / G-5500DC (control directo por el DIN)

Conector DIN de 8 pines «EXTERNAL CONTROL» de la parte trasera del mando (manual de Yaesu):

| Pin DIN | Función | A la placa |
|---|---|---|
| 6 | 2-4,5 V = acimut 0-450° | divisor → ADC AZ |
| 1 | 2-4,5 V = elevación 0-180° | divisor → ADC EL |
| 2 | a masa (pin 8) = girar derecha (CW) | colector de transistor de AZ CW |
| 4 | a masa = girar izquierda (CCW) | colector de AZ CCW |
| 3 | a masa = subir | colector de EL UP |
| 5 | a masa = bajar | colector de EL DOWN |
| 7 | 13-6 V, hasta 200 mA | (no hace falta) |
| 8 | masa común | GND de la placa |

**Lecturas de posición**: divisor de 10 kΩ (arriba) y 18 kΩ (abajo) en cada salida: 4,5 V → 2,89 V; 5 V → 3,21 V. Añade 100 nF a masa en la entrada del ADC. Los valores por defecto de la configuración ya cuentan con este divisor (1,286 V = 0°, 2,893 V = 450° / 180°); luego se afinan con «Punto A / Punto B» en la WebUI. Comprueba antes con un polímetro la tensión máxima del pin 6 con el rotor a tope en CW (el mando tiene un ajuste «OUT VOLTAGE ADJ»).

**Órdenes de giro**: un NPN (BC547 / 2N2222, base con 2,2 kΩ desde el pin de la placa) o un ULN2003 por línea, colector al pin del DIN y emisor a masa. Con transistores o ULN2003, `activeHigh: true`. Pon una resistencia de 10 kΩ de la base a masa (o usa el ULN2003, que ya la lleva): durante el arranque los pines quedan al aire y el rotor no debe moverse. Con optoacopladores (PC817) también: ánodo desde el pin con 330 Ω, colector al DIN y emisor a masa (aísla del todo las dos masas).

Configuración típica:

```json
"driver": 0,
"direct": { "outMode": 0, "activeHigh": true, "elEnabled": true,
  "az": { "min": 0, "max": 450, "reverseMs": 1200, "stallMs": 5000 },
  "el": { "min": 0, "max": 180 } }
```

El G-5500 necesita una pausa antes de invertir el giro (`reverseMs`, 1,2 s) y no tiene control de velocidad (salidas de relé; `minSpeed` no importa).

## Otros rotores con control directo

- **Rotor de un eje** (Yaesu G-450 / G-650 / G-1000DXC, Create RC5…): `elEnabled: false`. Los de la serie DXC y el G-2800 aceptan velocidad por tensión: pon un pin en `pinPwm` y filtra el PWM (RC de 10 kΩ / 10 µF) hacia la entrada de velocidad del mando.
- **Hy-Gain con freno** (Ham-IV, T2X): relé del freno en `brakePin`, `brakeMs` 2000-3000 (suelta el freno antes de girar y lo echa después de parar).
- **Motor de continua con puente H** (casero, motores de antena parabólica): `outMode: 1`; IN1 en `pinA` e IN2 en `pinB` con PWM (BTS7960, L298N, DRV8871). Arranque suave (`rampMs`) y frenada progresiva cerca del objetivo (`slowZone`, `minSpeed`).
- **Sensor ADS1115** (16 bits, I2C, recomendable si el ADC de la placa va justo): `sensor: 1`, canal en `adsCh`; ganancia `adsGain` 1 = ±4,096 V. Aliméntalo a 3,3 V y mantén el divisor.
- **Sin sensor** (`sensor: 2`): solo movimiento manual (botones, `M` de rotctld, `move` de la app), que se para solo a los 90 s; los topes los ponen los finales de carrera del rotor.

Seguridad del control directo: para en los topes (`limitMargin`), da fallo de atasco si no avanza `stallDeg` en `stallMs` y apaga todo si el sensor deja de leer. Tras un fallo, una orden de posición nueva o `clear` lo rearma.

## Pasarela serie

UART de la placa → controlador. Con controladores RS-232 (GS-232A/B, Rot2Prog, MD-01) pon un módulo MAX3232 a 3,3 V. Con placas TTL (K3NG, SatNOGS v3) directo, si son de 3,3 V; si son de 5 V, divisor en el RX.

| Controlador | `serial.proto` | Velocidad típica |
|---|---|---|
| Yaesu GS-232A | 0 | 9600 |
| Yaesu GS-232B, K3NG en modo Yaesu | 1 | 9600 |
| EasyComm II (K3NG, SatNOGS, ERC) | 2 | 9600 / 19200 |
| SPID Rot2Prog, MD-01, MD-02 | 3 | 600 (Rot2Prog) / 460800 (MD-01 USB) |

`serial.hasEl: false` para rotores solo de acimut; `minAz` / `maxAz` / `maxEl` son los que se anuncian a Hamlib.

## La placa como controlador para el PC

Por USB la placa habla `usb.proto` (por defecto GS-232B), así que **cualquier programa con Hamlib** la reconoce como un controlador comercial: `rotctl -m 603 -r /dev/ttyUSB0` (GS-232B), `-m 202` (EasyComm II), `-m 901` (Rot2Prog). Con `usb.proto: 5` el USB saca el registro de depuración.
