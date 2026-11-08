# Configuración de las placas (`/rotor_conf.json`)

Se edita entera desde la WebUI (*Configuración*) o la app (`cfgget` / `cfgset`); al guardar la placa se reinicia. Vale mandar solo las claves que cambian. Los campos secretos que aparecen como `********` no se tocan.

| Clave | Por defecto | Qué es |
|---|---|---|
| `name` | `APRS-Rotor` | nombre en la red (mDNS `APRS-Rotor.local`), en Bluetooth y punto de acceso |
| `pin` | `123456` | PIN de control (app y WebUI). Vacío = sin PIN |
| `wifi.mode` | 2 | 0 apagada, 1 cliente (si en 30 s no entra, abre el punto de acceso de rescate), 2 punto de acceso |
| `wifi.ssid` / `pass` | — | red de casa |
| `wifi.apPass` | `aprsrotor` | contraseña del punto de acceso (≥ 8) |
| `wifi.ntp` | `pool.ntp.org` | hora (necesaria para el código TOTP de APRS) |
| `net.rotctld` / `rotctldPort` | sí / 4533 | servidor rotctld (Hamlib NET rotctl) |
| `net.apiPort` | 4534 | protocolo JSON por TCP |
| `net.webUser` / `webPass` | `admin` / vacío (= PIN) | WebUI y actualización por `/update` |
| `usb.proto` | 1 | USB: 0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog, 4 rotctld, 5 registro (log) |
| `bt.mode` | 1 | 0 apagado, 1 BLE (app), 2 SPP (ESP32 original y Pico 2 W) |
| `bt.sppProto` | 1 | protocolo del SPP (como `usb.proto`, 0-4) |
| `driver` | 0 | 0 control directo, 1 pasarela serie |
| `led` | según placa | LED de estado: fijo moviéndose, parpadeo rápido con fallo |
| `lcd.bright` | 80 | brillo de la pantalla táctil, 10-100 % (solo placas con pantalla; también desde la pantalla de información) |
| `lcd.dimS` | 300 | segundos sin tocar la pantalla hasta bajar el brillo al 10 % (AMOLED: se apaga) (0 = nunca); el primer toque solo la enciende |

**Pines del expansor**: `100`-`115` son EXIO0-EXIO15 (solo salidas de nivel, sin PWM: `outMode` 0) y `200` es EXIO_ADC (lectura en cuentas). ESP32-C5-Touch-LCD-3.5: expansor CH32V, en el conector EXIO8-EXIO15 y EXIO_ADC ([WAVESHARE_C5_LCD35.md](WAVESHARE_C5_LCD35.md)). ESP32-S3-Touch-AMOLED-2.41: TCA9554, en el conector EXIO5-EXIO7, sin ADC ([WAVESHARE_S3_AMOLED241.md](WAVESHARE_S3_AMOLED241.md)).

## `direct` (control directo)

| Clave | Por defecto | |
|---|---|---|
| `outMode` | 0 | 0 relés / transistores (un pin por sentido), 1 puente H (PWM en IN1 / IN2) |
| `activeHigh` | true | nivel que activa una salida |
| `brakePin` | -1 | relé del freno (Hy-Gain) |
| `pwmFreq` | 1000 | Hz |
| `i2cSda` / `i2cScl` / `adsAddr` / `adsGain` | placa / 0x48 / 1 | ADS1115 |
| `elEnabled` | true | hay rotor de elevación |
| `parkAz` / `parkEl` | 0 / 0 | reposo |
| `reportMech` | false | devolver el acimut mecánico (> 360 en el solape) en vez de 0-360 |
| `az`, `el` | | ejes (abajo) |

Cada eje:

| Clave | Acimut | Elevación | |
|---|---|---|---|
| `min` / `max` | 0 / 450 | 0 / 180 | rango mecánico (con tope al sur: -180 / 180) |
| `rawA` → `degA`, `rawB` → `degB` | G-5500 con divisor | | calibración de dos puntos (lectura en bruto → grados); se fija con «Punto A / B» |
| `deadband` | 1,0° | | para dentro de este error |
| `restart` | 2,0° | | vuelve a moverse si el error supera esto (sin oscilar) |
| `slowZone` | 10° | | por debajo baja la velocidad hasta `minSpeed` (PWM) |
| `minSpeed` / `maxSpeed` | 0,35 / 1 | | velocidad del PWM (0-1) |
| `rampMs` | 600 | | arranque suave |
| `reverseMs` | 1200 | | pausa antes de invertir el giro |
| `stallMs` / `stallDeg` | 5000 / 1,0 | | atasco: no avanza `stallDeg` en `stallMs` → para y da fallo (0 = sin vigilancia) |
| `brakeMs` | 0 | | freno: tiempo para soltarlo y echarlo |
| `filter` | 0,35 | | filtro de la lectura (1 = sin filtro) |
| `limitMargin` | 0,5° | | para antes del tope |
| `pinA` / `pinB` | CW / CCW (UP / DOWN) | | salidas (puente H: IN1 / IN2) |
| `pinPwm` | -1 | | velocidad aparte (relés + entrada de velocidad del mando) |
| `sensor` | 0 | | 0 ADC de la placa, 1 ADS1115, 2 ninguno (solo manual) |
| `adcPin` / `adsCh` | placa / 0 (AZ), 1 (EL) | | |

## `serial` (pasarela)

`proto` (0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog), `baud`, `rx`, `tx`, `hasEl`, `pollMs` (500), `minAz` / `maxAz` / `maxEl` (los que se anuncian a Hamlib).

## `lora`

`chip` (0 ninguno, 1 SX1262, 2 SX1268, 3 SX1276, 4 SX1278), `freq` (433,775), `bw` (125), `sf` (12), `cr` (5), `power` (17 dBm), `tcxo` (V; 0 sin TCXO), pines `sck`, `miso`, `mosi`, `cs`, `rst`, `irq` (DIO1 en SX126x, DIO0 en SX127x), `busy`.

## `aprs`

`call` (indicativo del rotor), `tocall` (`APZLRR`), `path`, `allow`, `totp`, `reply`, `beaconMin`, `lat`, `lon`, `comment`. Ver [APRS_CONTROL.md](APRS_CONTROL.md).
