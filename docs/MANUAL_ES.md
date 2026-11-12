# APRS LoRa+ Rotor — Manual de usuario

*Versión 5.4.2 · © 2026 Manuel - EA7JRS · GPL-3.0-or-later · [English](MANUAL_EN.md) · [Guía rápida](QUICKSTART_ES.md)*

## Índice

1. [Qué es](#1-qué-es)
2. [Placas](#2-placas)
3. [Instalación del firmware](#3-instalación-del-firmware)
4. [Primera puesta en marcha](#4-primera-puesta-en-marcha)
5. [Conexión del rotor](#5-conexión-del-rotor)
6. [Calibración y ajuste del control](#6-calibración-y-ajuste-del-control)
7. [WebUI](#7-webui)
8. [Pantalla táctil](#8-pantalla-táctil)
9. [App Android](#9-app-android)
10. [Tracker APRS LoRa+](#10-tracker-aprs-lora)
11. [Programas con Hamlib y PC](#11-programas-con-hamlib-y-pc)
12. [Emisora y receptor SDR (CAT)](#12-emisora-y-receptor-sdr-cat)
13. [Control por mensajes APRS (LoRa)](#13-control-por-mensajes-aprs-lora)
14. [Raspberry Pi](#14-raspberry-pi)
15. [Seguridad](#15-seguridad)
16. [Actualizaciones y LED](#16-actualizaciones-y-led)
17. [Problemas frecuentes](#17-problemas-frecuentes)
18. [Referencia](#18-referencia)
19. [Red local, rumbo y hora](#19-red-local-rumbo-y-hora)

---

## 1. Qué es

**APRS LoRa+ Rotor** es un controlador de rotores de antena compatible con **Hamlib**. Forma parte del ecosistema APRS LoRa+ junto con el tracker y la app Android, y se puede manejar desde:

- el **tracker APRS LoRa+**: sigue satélites, estaciones APRS (también en movimiento) y balizas CW;
- la **app Android APRS LoRa+**, por Bluetooth (BLE) o WiFi;
- su **WebUI** en cualquier navegador;
- su propia **pantalla táctil**, en las placas Waveshare;
- **cualquier programa con Hamlib**: GPredict, Look4Sat, PstRotator, CSN S.A.T…;
- un **PC por USB**, donde se presenta como un controlador GS-232B, EasyComm II o Rot2Prog;
- **mensajes APRS por LoRa**, desde cualquier estación autorizada.

Puede mover el rotor de dos maneras:

- **Control directo**: la placa sustituye al ordenador del mando. Activa los relés o transistores de giro y lee la posición del potenciómetro del rotor.
- **Pasarela serie**: la placa habla con un controlador comercial (Yaesu GS-232A/B, SPID Rot2Prog / MD-01 / MD-02, EasyComm II: K3NG, SatNOGS, ERC…) o controladores con encóder en un bus RS485 con direcciones.

En la **Raspberry Pi** el rotor lo maneja el `rotctld` de Hamlib, así que sirve **cualquier rotor que Hamlib conozca**.

## 2. Placas

| | ESP32 | ESP32-S3 | ESP32-C5 LCD 3,5" | ESP32-S3 AMOLED 2,41" | ESP32-S3 LCD 7" | Pico 2 W | Pico 2 | Raspberry Pi |
|---|---|---|---|---| --- |---|---|---|
| Control directo | ✔ | ✔ | relés + 1 ADC (o ADS1115) | ✔ | relés PCF8574 (I2C) + 1 ADC (o ADS1115) | ✔ | ✔ | — |
| Pasarela serie | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | cualquier modelo de Hamlib |
| WiFi (rotctld, JSON, WebUI) | ✔ | ✔ | ✔ (2,4 y 5 GHz) | ✔ | ✔ | ✔ | — | ✔ |
| BLE para la app | ✔ | ✔ | ✔ | ✔ | ✔ | pendiente | — | ✔ |
| Bluetooth SPP (GS-232) para el tracker | ✔ | — | — | — | — | ✔ | — | ✔ |
| LoRa APRS | módulo externo | módulo externo | — | — | — | HAT SX1262 | HAT SX1262 | TNC KISS |
| Pantalla táctil | — | — | ✔ vertical | ✔ apaisada | ✔ apaisada 7" | — | — | — |
| USB como controlador | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | USB gadget (Zero 2 W) |
| Actualización por la WebUI | placas de 8 MB | ✔ | ✔ | ✔ | ✔ | ✔ | — | `git pull` + `install.sh` |

Entornos de PlatformIO: `esp32dev`, `esp32s3`, `waveshare_c5_lcd35`, `waveshare_s3_amoled241`, `waveshare_s3_lcd7b`, `pico2w`, `pico2`.

## 3. Instalación del firmware

### Instalador web (recomendado)

1. Abre **https://ea7jrs.github.io/APRS-LoRa-Plus/** en Chrome o Edge (necesitan Web Serial).
2. Elige **Rotor** y tu placa, conéctala por USB y pulsa **Instalar**. El instalador reconoce la placa conectada y la versión que lleva.
3. **Pico 2 / Pico 2 W**: descarga el fichero `.uf2`, conecta la placa con el botón **BOOTSEL** pulsado y copia el fichero a la unidad que aparece.

### PlatformIO

```sh
cd firmware
pio run -e esp32s3 -t upload        # o esp32dev, waveshare_c5_lcd35, waveshare_s3_amoled241, waveshare_s3_lcd7b, pico2w, pico2
```

### Raspberry Pi

Ver el [apartado 14](#14-raspberry-pi).

## 4. Primera puesta en marcha

1. Al arrancar por primera vez, la placa abre la WiFi **APRS-Rotor** (contraseña `aprsrotor`).
2. Conéctate a ella y abre **http://192.168.4.1/**. Usuario `admin`, contraseña = el PIN (`123456`).
3. En **Configuración** pulsa *Leer*, cambia lo necesario y pulsa *Guardar y reiniciar*:

| Clave | Qué poner |
|---|---|
| `name` | nombre de la caja: WiFi, Bluetooth y `nombre.local` |
| `pin` | **cámbialo**: protege la app, la WebUI y el control |
| `wifi.mode` | `1` para entrar en tu red (`wifi.ssid`, `wifi.pass`); `2` deja el punto de acceso; `0` apaga la WiFi |
| `aprs.call` | indicativo del rotor con SSID (por ejemplo `EA7JRS-12`) |
| `driver` | `0` control directo, `1` pasarela serie |
| `bt.mode` | `1` BLE (app), `2` SPP (tracker con ESP32 original), `0` apagado |

4. Tras reiniciar, la WebUI está en **http://APRS-Rotor.local/** (o en la IP que le dé tu router).

Si la placa no entra en tu WiFi en 30 s, abre el punto de acceso de rescate **APRS-Rotor** para que puedas corregirlo.

Todas las opciones están en [CONFIG.md](CONFIG.md). La configuración se guarda en la placa (`/rotor_conf.json`), y la app y la WebUI la leen y la escriben entera.

## 5. Conexión del rotor

> ⚠️ Las placas trabajan a **3,3 V**. Ninguna entrada puede recibir más: usa divisores de tensión, y transistores u optoacopladores para las salidas. Las líneas del mando y de los motores nunca van directas a un pin.

### 5.1 Pines por defecto

| Placa | AZ CW | AZ CCW | EL UP | EL DOWN | ADC AZ | ADC EL | I2C (ADS1115) | UART pasarela RX/TX |
|---|---|---|---|---|---|---|---|---|
| ESP32 DevKit | 25 | 26 | 27 | 14 | 34 | 35 | 21 / 22 | 16 / 17 |
| ESP32-S3 DevKitC-1 | 4 | 5 | 6 | 7 | 1 | 2 | 8 / 9 | 18 / 17 |
| ESP32-C5-Touch-LCD-3.5 | EXIO8 | EXIO9 | EXIO10 | EXIO11 | EXIO_ADC | ADS1115 | 27 / 26 | 12 / 11 |
| ESP32-S3-Touch-AMOLED-2.41 | 38 | 39 | 40 | 41 | 1 | 7 | 47 / 48 | 44 / 43 |
| ESP32-S3-Touch-LCD-7B | PCF8574 P0 (120) | P1 (121) | P2 (122) | P3 (123) | GPIO6 (Sensor) | ADS1115 | 8 / 9 | 44 / 43 |
| Pico 2 W / Pico 2 | GP6 | GP7 | GP8 | GP9 | GP26 | GP27 | GP4 / GP5 | GP1 / GP0 |

Todos se cambian en la configuración; `-1` significa sin usar. En el ESP32, usa solo pines del ADC1, porque el ADC2 no funciona con la WiFi encendida. Las placas con pantalla tienen su propia hoja: [WAVESHARE_C5_LCD35.md](WAVESHARE_C5_LCD35.md), [WAVESHARE_S3_AMOLED241.md](WAVESHARE_S3_AMOLED241.md) y [WAVESHARE_S3_LCD7B.md](WAVESHARE_S3_LCD7B.md). En la de 7" los relés van por una placa PCF8574 en el I2C (pines 120-127, activan con 0: `direct.activeHigh: false`).

### 5.2 Control directo: Yaesu G-5500 por el DIN de 8 pines

Conector «EXTERNAL CONTROL» de la parte trasera del mando:

| Pin DIN | Función | A la placa |
|---|---|---|
| 6 | 2-4,5 V = acimut 0-450° | divisor → ADC AZ |
| 1 | 2-4,5 V = elevación 0-180° | divisor → ADC EL |
| 2 | a masa = CW | transistor de AZ CW |
| 4 | a masa = CCW | transistor de AZ CCW |
| 3 | a masa = UP | transistor de EL UP |
| 5 | a masa = DOWN | transistor de EL DOWN |
| 8 | masa común | GND |

- **Posición**: en cada salida, un divisor de 10 kΩ (arriba) y 18 kΩ (abajo), con 100 nF a masa en la entrada del ADC. Los valores por defecto ya cuentan con este divisor.
- **Giro**: un NPN (BC547 / 2N2222, con 2,2 kΩ en la base y 10 kΩ de la base a masa) o un ULN2003 por línea. El colector va al pin del DIN y el emisor a masa. Con optoacopladores (PC817) las dos masas quedan aisladas.
- El G-5500 necesita una pausa antes de invertir el giro (`reverseMs` 1200).

### 5.3 Otros rotores con control directo

- **Un solo eje** (G-450, G-650, G-1000DXC…): `direct.elEnabled: false`.
- **Velocidad variable** (serie DXC, G-2800): pin en `pinPwm`, con un filtro RC hacia la entrada de velocidad del mando.
- **Hy-Gain con freno** (Ham-IV, T2X): relé del freno en `brakePin` y `brakeMs` de 2000 a 3000.
- **Motor de continua con puente H** (BTS7960, L298N, DRV8871): `outMode: 1`, con arranque suave (`rampMs`) y frenada progresiva (`slowZone`, `minSpeed`).
- **ADS1115** (16 bits por I2C): `sensor: 1` y el canal en `adsCh`.
- **Sin sensor** (`sensor: 2`): solo movimiento manual, que se para solo a los 90 s.

### 5.4 Pasarela serie

UART de la placa → controlador. Con RS-232, pon un MAX3232 a 3,3 V; con TTL de 5 V, un divisor en el RX.

| Controlador | `serial.proto` | Velocidad |
|---|---|---|
| Yaesu GS-232A | 0 | 9600 |
| Yaesu GS-232B, K3NG en modo Yaesu | 1 | 9600 |
| EasyComm II (K3NG, SatNOGS, ERC) | 2 | 9600 / 19200 |
| SPID Rot2Prog, MD-01, MD-02 | 3 | 600 / 460800 |
| Bus RS485 con direcciones (un controlador por eje; `serial.busId`, `azId`, `elId`) | 4 | 115200 |

`serial.hasEl: false` en rotores solo de acimut. `minAz`, `maxAz` y `maxEl` son los límites que se anuncian a Hamlib.

Más detalles en [HARDWARE.md](HARDWARE.md).

## 6. Calibración y ajuste del control

### Calibración de dos puntos

Desde la WebUI (*Calibración*) o la app (*Rotor* → calibración):

1. Lleva el rotor con los botones a un extremo conocido (por ejemplo 0°), escribe los grados y pulsa **Punto A aquí**.
2. Llévalo al otro extremo (por ejemplo 450° o 180°) y pulsa **Punto B aquí**.
3. **Corregir desfase**: si solo está desviado unos grados, escribe dónde apunta realmente y púlsalo.

La lectura en bruto (milivoltios o cuentas) se muestra junto a la posición.

### Ajustes de cada eje (`direct.az`, `direct.el`)

| Ajuste | Por defecto | Para qué |
|---|---|---|
| `min` / `max` | 0 / 450 (AZ), 0 / 180 (EL) | rango mecánico; con tope al sur, -180 / 180 |
| `deadband` | 1,0° | para cuando el error es menor |
| `restart` | 2,0° | vuelve a moverse si el error supera esto (evita oscilar) |
| `slowZone`, `minSpeed`, `maxSpeed` | 10°, 0,35, 1 | frenada cerca del objetivo (PWM) |
| `rampMs` | 600 | arranque suave |
| `reverseMs` | 1200 | pausa antes de invertir |
| `stallMs` / `stallDeg` | 5000 / 1,0 | atasco: si no avanza, para y da fallo |
| `limitMargin` | 0,5° | para antes del tope |
| `filter` | 0,35 | filtro de la lectura (1 = sin filtro) |

**Solape (rotores de 450°)**: si se pide un rumbo de 0 a 360°, el control elige el giro más corto que no pase por los topes. Con `reportMech: true`, el rotor devuelve el acimut mecánico (más de 360 en la zona de solape).

## 7. WebUI

En `http://APRS-Rotor.local/` (placas) o en `http://IP:8080/` (Raspberry Pi). Usuario `admin`, contraseña = PIN (o `net.webPass`).

| Sección | Qué hace |
|---|---|
| **Posición** | acimut y elevación actuales, objetivo, estado y lectura en bruto |
| **Mando** | **Ir** a un acimut y una elevación; CCW / CW / UP / DOWN mientras se mantienen pulsados; **STOP**; **Park**; **Borrar fallo** |
| **Calibración** | Punto A, Punto B y corregir el desfase (control directo) |
| **CAT** | (con `cat.proto`) frecuencia, paso, modos, A/B, tono CTCSS, S-meter, PTT y seguimiento con Doppler |
| **Configuración** | el fichero entero: *Leer*, editar, *Guardar y reiniciar*; las contraseñas aparecen como `********` y no se tocan |
| **Actualizar firmware** | subir un `firmware.bin` |
| **Acerca de** | versión, placa y © 2026 Manuel - EA7JRS |

## 8. Pantalla táctil

Placas **Waveshare ESP32-C5-Touch-LCD-3.5** (vertical), **ESP32-S3-Touch-AMOLED-2.41** y **ESP32-S3-Touch-LCD-7B** (apaisadas).

- **Brújula**: aguja roja = posición, azul = objetivo, arco naranja = zona de solape.
- **Botones**: **CCW · STOP · CW · DOWN · PARK · UP**. Los de movimiento mueven mientras se mantienen pulsados.
- **Apuntar**: mantén la brújula pulsada medio segundo, arrastra hasta el rumbo (aguja verde) y suelta. Un toque corto no mueve nada.
- **Fallo**: aparece en rojo; tócalo para borrarlo.
- **CAT** (botón de la cabecera, con `cat.proto`): frecuencia, − / paso / +, LSB · USB · CW · FM · AM · DIG, *Seguir* (Doppler), A / B y PTT manteniendo pulsado. El tono CTCSS se ve junto al nombre de la emisora.
- **ⓘ**: nombre, indicativo, placa, WiFi, IP, puertos, Bluetooth, tiempo encendida, **Acerca de** y brillo.
- **Colores**: de serie, la paleta de la app Android (la misma que APRS LoRa+ Display); con `lcd.theme` 1, la de la WebUI (cabecera roja).
- **Brillo y apagado**: `lcd.bright` y `lcd.dimS`. La LCD se atenúa; la AMOLED se apaga para que no queden marcas. El primer toque solo la enciende.

## 9. App Android

Pestaña **Rotor** de la app APRS LoRa+:

1. **Conectar**: *Scan for rotor boxes* (BLE) o *Address* = `APRS-Rotor.local` / IP (WiFi, puerto 4534).
2. **PIN**: el de la caja (`pin`). Sin PIN correcto solo se puede consultar.
3. **Mando**: brújula, ir a, mover manteniendo pulsado, STOP, Park, *Clear fault*.
4. **Follow**: la caja sigue
   - la puntería del tracker (satélite, GO TO, objetivo fijo…);
   - una estación APRS de la lista del tracker, una concreta o la última que pase el filtro, con estimación si se mueve;
   - una baliza CW con locator (`VVV DE ED7YAA IM76HR`), del decodificador del tracker o del móvil.

   Los ángulos se calculan desde el GNSS del tracker.
5. **Radio (CAT)**: frecuencia, paso, modos, A/B, *split*, tono CTCSS, S-meter, PTT y Doppler del satélite del tracker ([apartado 12](#12-emisora-y-receptor-sdr-cat)).
6. **Calibración** (Point A / Point B / Offset), **configuración** completa (*Read*, *Save and restart*) y *Restart the rotor box*.
7. **Viento**: con el tiempo de internet de la app activado (*Sensors > Internet weather*, OpenWeatherMap o IQAir) la app manda el viento a la caja; con `wind.on: true` la caja aparca y bloquea el rotor con rachas de `wind.park` m/s o más (ver [apartado 15](#15-seguridad)).

No dejes que el tracker mueva el mismo rotor a la vez por WiFi o SPP: elige una sola vía.

## 10. Tracker APRS LoRa+

En la WebUI del tracker, **Antenna rotor**:

| Enlace | Rotor | Tracker |
|---|---|---|
| WiFi (rotctld) | `net.rotctld` activado (puerto 4533) | *Transport* = TCP, *Host* = `APRS-Rotor.local` o IP, *Port* = 4533 |
| Bluetooth SPP (tracker con ESP32 original) | `bt.mode: 2`, `bt.sppProto: 1` | *Transport* = Bluetooth SPP, *BT name* = nombre del rotor |
| App como puente | `bt.mode: 1` | *Send the rot event*; la app reenvía con *Follow* |

Qué sigue (*What the rotor follows*):

- **Satélite** del modo SAT, desde unos segundos antes del AOS hasta el LOS.
- **Estación con GO TO**, con su posición actualizada cada vez que se oye.
- **Objetivo fijo** (repetidor, baliza…).
- **Última estación APRS oída** (LoRa o FSK), con filtro de indicativos y antigüedad máxima.
- **Última baliza CW con locator** (modo MORSE, escucha CW).
- **GPredict a través del tracker**.

**Estaciones en movimiento**: el rotor apunta a la posición **estimada ahora**, calculada con la velocidad y el rumbo de su última posición durante el tiempo transcurrido (300 s como máximo). El nombre lleva `~` mientras es estimada.

## 11. Programas con Hamlib y PC

- **Por WiFi**: en GPredict, Look4Sat, PstRotator…, Hamlib «NET rotctl» (modelo 2) con `IP:4533`. Admite hasta 4 clientes a la vez. Prueba: `rotctl -m 2 -r APRS-Rotor.local:4533 p`.
- **Por USB**: la placa se presenta como un controlador según `usb.proto` (GS-232B por defecto): `rotctl -m 603 -r /dev/ttyUSB0`, `-m 202` (EasyComm II) o `-m 901` (Rot2Prog). Con `usb.proto: 5`, el USB saca el registro de depuración.

## 12. Emisora y receptor SDR (CAT)

La caja maneja también una emisora o un receptor SDR: frecuencia, modo, VFO A/B, *split*, S-meter, tono CTCSS, PTT y Doppler de satélites. Detalles, conexiones y modelos en [CAT.md](CAT.md).

| `cat.proto` | Equipos | Enlace |
|---|---|---|
| 1 Icom CI-V | IC-705, IC-7300, IC-7610, IC-9700, IC-910… (`cat.civ` = dirección: A2 IC-9700, 94 IC-7300, A4 IC-705) | UART |
| 2 Yaesu | FT-991/A, FT-710, FTDX10, FTDX101, FT-891 | UART |
| 3 Yaesu 5 bytes | FT-817/818, FT-857, FT-897 | UART |
| 4 Kenwood / Elecraft | TS-2000, TS-480, TS-590, TS-890, K3, KX2, KX3, QMX | UART |
| 5 SDRconnect | **SDRplay RSPdx-R2** y demás RSP (API WebSocket, puerto 5454) | WiFi |
| 6 rigctl | SDR++ (4532), gqrx (7356), otro rigctld | WiFi |

En la Raspberry Pi, `cat.type: "hamlib"` usa el `rigctld` real de Hamlib con cualquier emisora (`cat.model` = número de `rigctl -l`).

1. **Conecta** la emisora a `cat.rx` / `cat.tx` (3,3 V; con RS-232 pon un MAX3232; CI-V con su interfaz TTL). Masa común.
2. **Configura** `cat.proto`, `cat.baud` (el de la emisora), `cat.civ` (Icom) y `cat.model` (nombre que se ve). Para SDRconnect o rigctl: `cat.host` y `cat.port`.
3. **Usa**: botón **CAT** de la pantalla táctil, tarjeta *CAT* de la WebUI, *Rotor > Radio (CAT)* en la app, o **rigctld** de la caja (puerto 4532) desde GPredict, WSJT-X…
4. **Satélites**: con el tracker en modo SAT, la app manda a la caja el transmisor elegido y la velocidad radial; la caja corrige el Doppler de bajada (y de subida), y al cambiar de satélite pone el modo, el *split* y el tono CTCSS (el que se escriba o el de la descripción del transmisor). GPredict puede hacerlo directamente con el rigctld de la caja.

**PTT**: apagado por defecto (`cat.ptt: false`). Activado, solo emite mientras se mantiene pulsado y se suelta solo a los `cat.pttMaxS` segundos. Los receptores (SDRconnect, SDR++) no tienen PTT.

## 13. Control por mensajes APRS (LoRa)

En placas con módulo LoRa (ESP32 / ESP32-S3 con módulo externo, Pico con el HAT SX1262) o en la Raspberry Pi con un TNC KISS. Frecuencia por defecto: 433,775 MHz, SF12, BW 125 kHz, CR 4/5 (`lora.*`).

Manda un mensaje APRS al indicativo del rotor (`aprs.call`):

| Mensaje | Hace |
|---|---|
| `P 123 45` / `AZ 123 EL 45` | ir a acimut 123°, elevación 45° |
| `P 123` | solo acimut |
| `PARK` / `STOP` | reposo / parar |
| `?` | posición actual |
| `HELP` | lista de órdenes |

- **Quién puede mandar**: los indicativos de `aprs.allow`. `EA7JRS` vale para cualquier SSID; con la lista vacía, solo el indicativo base del rotor.
- **Código TOTP** (opcional, `aprs.totp`): secreto en base32 de cualquier app de autenticación. Se añade al final: `P 123 45 492039`. No se puede reutilizar y necesita la hora (NTP).
- **Respuestas**: `ack` y `GO AZ 123 EL 45`, `… OK`, `… MOVING` o `ERR …`.
- **Baliza de estado** cada `aprs.beaconMin` minutos.

LoRa APRS es lento: no sirve para seguir satélites. Más detalles en [APRS_CONTROL.md](APRS_CONTROL.md).

## 14. Raspberry Pi

```sh
git clone https://github.com/EA7JRS/APRS-LoRa-Plus-Rotor
cd APRS-LoRa-Plus-Rotor/raspberrypi
sudo ./install.sh              # Zero 2 W como GS-232B por USB para el PC: sudo ./install.sh --gadget
```

1. Busca tu controlador en `rotctl -l` (601 GS-232A, 603 GS-232B, 202 EasyComm II, 901 Rot2Prog, 903 MD-01/02, 1 Dummy para probar).
2. En la WebUI (`http://IP:8080`) o en `/etc/aprs-lora-plus-rotor/config.json`:
   ```json
   "hamlib": { "model": 603, "device": "/dev/serial/by-id/...", "speed": 9600 }
   ```
3. Puertos: rotctld 4533, JSON 4534, WebUI 8080. Bluetooth: BLE para la app y SPP para el tracker. APRS por un TNC KISS (`aprs.kiss` = `tcp:IP:8001` del tracker o iGate en KISS, o `serial:/dev/ttyACM0:115200`).
4. Registro: `journalctl -u aprs-lora-plus-rotor -f`.

Más detalles en [RASPBERRY_PI.md](RASPBERRY_PI.md).

## 15. Seguridad

- **Cambia el PIN** (`pin`): protege la app, la WebUI y el protocolo JSON. Si la WebUI va a ser accesible fuera de tu red, pon también `net.webPass`.
- **El rotor se protege solo**: para en los topes (`limitMargin`), da fallo si se atasca (`stallMs`) y lo apaga todo si el sensor deja de leer. Tras un fallo, una orden de posición nueva o *Borrar fallo* lo rearma.
- **Al arrancar**, los pines quedan al aire un instante: pon resistencias de 10 kΩ de la base a masa (o usa un ULN2003) para que el rotor no se mueva.
- **Viento** (`wind`): `on` (apagado por defecto), `park` (m/s de racha para aparcar, 15), `resume` (m/s para volver, 10), `hold` (minutos sin rachas fuertes, 15). Mientras está aparcado la pantalla, la WebUI y la app muestran «WIND» y el viento, y el rotor rechaza cualquier movimiento (STOP y Park sí). Sin datos nuevos sigue aparcado; *Borrar fallo* lo libera a mano hasta la siguiente racha fuerte. El dato es de internet (la estación meteorológica más cercana, cada 15 min): no sustituye a un anemómetro en la torre.
- **Anemómetro en la caja** (`wind.anemoPin`, `wind.kmhPerHz`, `wind.pullup`): uno de cazoletas con ampolla reed (cierra el contacto al pasar el imán), entre un pin de la placa y masa. La caja cuenta los impulsos y cada segundo pasa a la protección el viento medio de 2 min y la racha (la mayor media de 3 s de los últimos 2 min); el tiempo de internet de la app deja de usarse. `kmhPerHz` = km/h por impulso por segundo: 2,4 en los de las estaciones baratas (Misol, Fine Offset, SparkFun), 3,62 en el Davis 6410; si no lo sabes, compáralo con otro anemómetro: *Datos de la caja* muestra los impulsos por segundo. Conexión en [HARDWARE.md](HARDWARE.md). Un cable cortado da 0 m/s: compruébalo de vez en cuando.
- **PTT de la emisora**: apagado salvo `cat.ptt: true`; solo mientras se mantiene pulsado y con límite de tiempo (`cat.pttMaxS`).
- **APRS**: lista de indicativos y, mejor, código TOTP. El contenido viaja sin cifrar, como piden las normas de radioaficionado.

## 16. Actualizaciones y LED

- **WebUI → Actualizar firmware** (o `http://APRS-Rotor.local/update`): sube el `firmware.bin` de la release. No está disponible en los ESP32 de 4 MB; usa el instalador web.
- **Instalador web**: siempre vale y conserva la configuración, salvo que elijas borrar.
- **Versión**: todos los proyectos APRS LoRa+ llevan el mismo número; actualiza a la vez el tracker, el rotor y la app.
- **LED de estado** (placas que lo tienen): fijo = moviéndose; parpadeo rápido = fallo; destello lento = en reposo.

## 17. Problemas frecuentes

| Síntoma | Qué hacer |
|---|---|
| No veo la WiFi APRS-Rotor | Espera 30 s tras encender; si estaba en modo cliente y no entra en tu red, vuelve a abrirla |
| `APRS-Rotor.local` no responde | Usa la IP (en tu router o en la pantalla ⓘ); Android antiguo no resuelve `.local` |
| No se mueve y da fallo | *Borrar fallo*; revisa topes, atasco (`stallMs`) y la lectura en bruto |
| Gira al revés | Intercambia `pinA` / `pinB` o recalibra los puntos A y B |
| La lectura salta | 100 nF en la entrada del ADC, `filter` más bajo, ADS1115 |
| Se pasa y vuelve | Sube `deadband` / `restart`; con PWM, amplía `slowZone` |
| La app no manda órdenes | PIN distinto del de la caja; *Send PIN* |
| GPredict no conecta | Puerto 4533, `net.rotctld: true`, misma red, cortafuegos |
| La emisora no responde (CAT) | Baudios iguales en la caja y la emisora (CI-V: no «Auto»), dirección CI-V, RX/TX cruzados, masa común; con SDRconnect, su API activada y el puerto 5454 |
| El tracker no mueve el rotor | *Antenna rotor* activado, transporte y host correctos; prueba `rotctl -m 2 -r IP:4533 p` desde un PC |
| APRS responde `ERR no time` | Sin hora para el TOTP: la placa necesita WiFi con NTP |
| He olvidado el PIN | Instala de nuevo con el instalador web eligiendo **borrar** (vuelve a la configuración de fábrica) |

## 18. Referencia

| Puerto | Servicio |
|---|---|
| 80 (placas) / 8080 (Raspberry Pi) | WebUI y `/update` |
| 4533 | rotctld (Hamlib NET rotctl) |
| 4534 | protocolo JSON (app por WiFi) |
| 4532 | rigctld de la emisora (Hamlib NET rigctl, con CAT) |
| BLE `4c6f5261-2b00-4ea7-b000-000000000001` | protocolo JSON (app) |

Documentos: [CONFIG.md](CONFIG.md) (todas las opciones) · [CAT.md](CAT.md) (emisoras y SDR) · [HARDWARE.md](HARDWARE.md) (conexiones) · [CONTROL_PROTOCOL.md](CONTROL_PROTOCOL.md) (protocolo JSON) · [APRS_CONTROL.md](APRS_CONTROL.md) · [INTEGRATION.md](INTEGRATION.md) · [RASPBERRY_PI.md](RASPBERRY_PI.md) · [WAVESHARE_C5_LCD35.md](WAVESHARE_C5_LCD35.md) · [WAVESHARE_S3_AMOLED241.md](WAVESHARE_S3_AMOLED241.md) · [WAVESHARE_S3_LCD7B.md](WAVESHARE_S3_LCD7B.md).

**Acerca de**: APRS LoRa+ Rotor · © 2026 Manuel - EA7JRS · GPL-3.0-or-later · https://github.com/EA7JRS/APRS-LoRa-Plus. Formatos de protocolo tomados de Hamlib; letras de la pantalla: DejaVu Sans.

## 19. Red local, rumbo y hora

La caja usa la misma conectividad que el plotter y APRS LoRa+ Display (`firmware/lib/aprs_links` y `firmware/lib/plotter_core`, copiadas del plotter con `tools/sync_links.sh`). Va con la WiFi de la caja, como cliente o con su punto de acceso; en la Pico no está. En la Raspberry la hace `lan.py`. Se configura en el grupo `lan` (WebUI, app > *Rotor* > configuración, [CONFIG.md](CONFIG.md)):

- **Servidor TCP** (`lan.kind`): 1 NMEA 0183 (`lan.port` 10110), 2 gpsd (2947), 3 Signal K (8375). Con `lan.host` vacío se conecta al equipo que da la WiFi: el móvil con la app y su servidor NMEA encendido.
- **UDP** (`lan.udp`): NMEA que se difunde en la red, por ejemplo el instrumento de viento de 4″ (10110).
- **OpenWeatherMap** (`lan.owmKey`): el tiempo de internet sin la app, en la posición de la red o en `aprs.lat` / `aprs.lon`.

Para qué sirve:

1. **Viento** (`lan.wind`, de serie sí, con `wind.on`): el mayor de viento aparente y real que lleguen por la red (MWV, Signal K) o el de OpenWeatherMap va a la protección por viento. El medido manda sobre el tiempo de la app (`wx` se contesta `ok` y no se usa); el anemómetro de la caja, sobre todo.
2. **Posición** (`lan.pos`, de serie sí): la de gpsd, el NMEA o Signal K sustituye a `aprs.lat` / `aprs.lon` en la baliza APRS y en la consulta del tiempo.
3. **Rumbo** (`lan.heading`, de serie no): con el rotor en un barco o un coche, el acimut que se pide y el que se da son **verdaderos**. La caja resta el rumbo del barco (HDT, VHW o `navigation.headingTrue`; con un compás magnético HDG / HDM, más `lan.magVar`) y vuelve a apuntar cuando el rumbo cambia `lan.headStep` grados (3) o más. Parar, aparcar o mover a mano dejan de seguir. Sin rumbo nuevo sigue con el último (`hdg.old` en el evento `pos`). El reposo (`parkAz`) sigue siendo respecto a la proa.

**Hora**: el reloj de la caja se pone con la hora del GPS que llegue por la red (RMC / ZDA de gpsd, del móvil o del plotter) y, sin GPS, con NTP: `wifi.ntp` es de serie `ntp.roa.es`, la hora oficial de España (Real Instituto y Observatorio de la Armada), con `hora.roa.es` y `pool.ntp.org` de reserva. Mientras la del GPS esté al día (6 h), NTP no la sustituye. El evento `info` dice de dónde viene (`clock`). En la Raspberry, `install.sh` pone **NTPsec** con `nts1.roa.es` (hora firmada con NTS), `hora.roa.es`, `minuto.roa.es`, `pool.ntp.org` y el GPS de gpsd si lo hay (con PPS, del orden del microsegundo); `NO_NTP=1` lo deja como estaba. `ntpq -p` enseña los servidores.
