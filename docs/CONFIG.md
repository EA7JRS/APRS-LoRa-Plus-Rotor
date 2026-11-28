# Configuración de las placas (`/rotor_conf.json`)

Se edita entera desde la WebUI (*Configuración*) o la app (`cfgget` / `cfgset`); al guardar la placa se reinicia. Vale mandar solo las claves que cambian. Los campos secretos que aparecen como `********` no se tocan.

| Clave | Por defecto | Qué es |
|---|---|---|
| `name` | `APRS-Rotor` | nombre en la red (mDNS `APRS-Rotor.local`), en Bluetooth y punto de acceso |
| `pin` | `123456` | PIN de control (app y WebUI). Vacío = sin PIN |
| `wifi.mode` | 2 | 0 apagada, 1 cliente (si en 30 s no entra, abre el punto de acceso de rescate), 2 punto de acceso |
| `wifi.ssid` / `pass` | — | red de casa |
| `wifi.apPass` | `aprsrotor` | contraseña del punto de acceso (≥ 8) |
| `wifi.ntp` | `ntp.roa.es` | NTP (hora oficial de España, ROA; de reserva `hora.roa.es` y `pool.ntp.org`). Necesaria para el código TOTP de APRS. La hora del GPS de la red (`lan`) manda sobre NTP |
| `net.rotctld` / `rotctldPort` | sí / 4533 | servidor rotctld (Hamlib NET rotctl) |
| `net.apiPort` | 4534 | protocolo JSON por TCP |
| `net.webUser` / `webPass` | `admin` / vacío (= PIN) | WebUI y actualización por `/update` |
| `usb.proto` | 1 | USB: 0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog, 4 rotctld, 5 registro (log) |
| `bt.mode` | 1 | 0 apagado, 1 BLE (app), 2 SPP (ESP32 original y Pico 2 W) |
| `bt.sppProto` | 1 | protocolo del SPP (como `usb.proto`, 0-4) |
| `driver` | 0 | 0 control directo, 1 pasarela serie, 2 rotctld de otra caja por la red (`remote`; placas con WiFi) |
| `remote.host` / `port` | — / 4533 | `driver` 2: IP o nombre del rotctld (la Raspberry Pi con el Stepper HAT, otra caja, cualquier rotctld de Hamlib). Los límites salen de su `\dump_state` (hasta entonces, `serial.minAz` / `maxAz` / `maxEl`); `serial.pollMs` = cada cuánto se lee la posición. Ver [RASPBERRY_HATS.md](RASPBERRY_HATS.md#emparejar-la-pi-con-una-pantalla-o-con-la-app) |
| `led` | según placa | LED de estado: fijo moviéndose, parpadeo rápido con fallo |
| `lcd.bright` | 80 | brillo de la pantalla táctil, 10-100 % (solo placas con pantalla; también desde la pantalla de información) |
| `lcd.theme` | 0 | paleta de la pantalla: 0 = la de la app Android (la misma que APRS LoRa+ Display), 1 = la de la WebUI (cabecera roja); cambia al momento |
| `lcd.dimS` | 300 | segundos sin tocar la pantalla hasta bajar el brillo al 10 % (AMOLED: se apaga) (0 = nunca); el primer toque solo la enciende |

**Pines del PCF8574 externo**: `120`-`127` (`direct.pcfAddr`). **Pines del expansor**: `100`-`115` son EXIO0-EXIO15 (solo salidas de nivel, sin PWM: `outMode` 0) y `200` es EXIO_ADC (lectura en cuentas). ESP32-C5-Touch-LCD-3.5: expansor CH32V, en el conector EXIO8-EXIO15 y EXIO_ADC ([WAVESHARE_C5_LCD35.md](WAVESHARE_C5_LCD35.md)). ESP32-S3-Touch-AMOLED-2.41: TCA9554, en el conector EXIO5-EXIO7, sin ADC ([WAVESHARE_S3_AMOLED241.md](WAVESHARE_S3_AMOLED241.md)).

## `direct` (control directo)

| Clave | Por defecto | |
|---|---|---|
| `outMode` | 0 | 0 relés / transistores (un pin por sentido), 1 puente H (PWM en IN1 / IN2) |
| `activeHigh` | true | nivel que activa una salida |
| `brakePin` | -1 | relé del freno (Hy-Gain) |
| `pwmFreq` | 1000 | Hz |
| `i2cSda` / `i2cScl` / `adsAddr` / `adsGain` | placa / 0x48 / 1 | ADS1115 |
| `pcfAddr` | 0x20 | PCF8574 de una placa de relés: pines 120-127 = P0-P7 (no 0x24 en las placas con expansor en esa dirección) |
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

`proto` (0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog, 4 bus RS485), `baud`, `rx`, `tx`, `hasEl`, `pollMs` (500), `busId` / `azId` / `elId` (3 / 20 / 21: direcciones en el bus RS485), `minAz` / `maxAz` / `maxEl` (los que se anuncian a Hamlib).

`rs485` (`false`): GS-232A/B, EasyComm II o Rot2Prog por un **RS485 de dos hilos** (semidúplex): una trama cada vez, la siguiente tras la respuesta (o 250 ms) o tras lo que tarda en salir. En el otro extremo, un convertidor RS485 ↔ RS232 / TTL al controlador. El bus propio (`proto` 4) ya va así. `de` (-1): GPIO del DE / RE de un transceptor sin conmutación automática (RTS de la UART en modo RS485 semidúplex; solo ESP32); -1 en la LCD-7 / 7B, cuyo transceptor conmuta solo.

## `log` (registro de movimientos en la microSD; LCD-7 y LCD-7B)

| Clave | Por defecto | |
|---|---|---|
| `sd` | `true` en la LCD-7, `false` en la 7B | un CSV por día (UTC) en `/rotor/AAAA-MM-DD.csv` de la tarjeta (FAT32); sin hora, `/rotor/nodate.csv` con los segundos desde el arranque |
| `cs` | 6 | GPIO de relleno para el CS de la librería SD (el de verdad es EXIO4 del expansor): no se conecta nada. Es el del conector *Sensor*: con `driver` 0 y el potenciómetro ahí (lo de serie en la 7B), elige otro libre o deja `sd` en `false` |
| `everyS` | 5 | una línea de posición cada tantos segundos mientras se mueve |
| `keepDays` | 90 | borra los días más antiguos (0 = guardarlo todo) |

Columnas: `utc,event,az,el,taz,tel,fault`. Sucesos: `BOOT`, `MOVE` (objetivo nuevo), `MANUAL` (se mueve sin objetivo), `POS`, `STOP`, `FAULT`, `CLEAR`. Se ve y se descarga en la WebUI: `http://<caja>/sd` (enlace en la línea de información). Evento `info`: `caps.sd` y `sd` (`on`, `ok`, `file`, `freeMB`, `lines`).

## `lora`

`chip` (0 ninguno, 1 SX1262, 2 SX1268, 3 SX1276, 4 SX1278), `freq` (433,775), `bw` (125), `sf` (12), `cr` (5), `power` (17 dBm), `tcxo` (V; 0 sin TCXO), pines `sck`, `miso`, `mosi`, `cs`, `rst`, `irq` (DIO1 en SX126x, DIO0 en SX127x), `busy`.

## `cat` (emisora o receptor SDR)

| Clave | Por defecto | |
|---|---|---|
| `proto` | 0 | 0 apagado, 1 Icom CI-V, 2 Yaesu (texto), 3 Yaesu 5 bytes (FT-817/857/897), 4 Kenwood / Elecraft, 5 SDRconnect (SDRplay), 6 rigctl por red |
| `baud` | 19200 | baudios de la emisora (1-4) |
| `rx` / `tx` | según placa | pines de la UART (1-4); ver [CAT.md](CAT.md) |
| `civ` | 162 (0xA2) | dirección CI-V de la emisora (Icom) |
| `model` | vacío | nombre que se ve (`IC-9700`); en Kenwood, si contiene 480 / 590 / 890 / 990, el tono CTCSS se numera desde 00 |
| `host` / `port` | — / 0 | SDRconnect (5454) o rigctld (4532) de la red (5-6); 0 = el puerto por defecto |
| `pollMs` | 120 | pausa entre consultas |
| `ptt` | false | permitir transmitir desde la pantalla, la WebUI, la app y rigctld |
| `pttMaxS` | 180 | el PTT se suelta solo pasado este tiempo |
| `trackStep` | 50 | Hz mínimos para resintonizar al seguir el Doppler |
| `rigctld` / `rigctldPort` | true / 4532 | servidor rigctld de la emisora |

## `wind` (protección por viento)

| Clave | Por defecto | |
|---|---|---|
| `on` | false | aparcar y bloquear el rotor con viento fuerte |
| `park` | 15 | racha (o viento medio si no hay racha), m/s, para aparcar |
| `resume` | 10 | por debajo de esto, m/s… |
| `hold` | 15 | …y tras estos minutos sin rachas fuertes, vuelve a aceptar movimientos |
| `anemoPin` | -1 | pin del anemómetro de cazoletas (ampolla reed a masa); -1 = se usa el tiempo de internet de la app. Un GPIO de la placa (no del expansor) que admita interrupciones |
| `kmhPerHz` | 2.4 | km/h por impulso por segundo (Misol / Fine Offset 2,4; Davis 6410 3,62) |
| `pullup` | true | resistencia interna a 3V3 (con cable largo, mejor una externa de 4,7 k) |

## `lan` (red local: viento, posición, rumbo; ESP32 y Raspberry Pi)

| Clave | Por defecto | |
|---|---|---|
| `kind` | 0 | servidor TCP: 0 ninguno, 1 NMEA 0183, 2 gpsd, 3 Signal K |
| `host` | "" | dirección del servidor; vacío = el equipo que da la WiFi (el móvil con la app) |
| `port` | 10110 | 10110 NMEA, 2947 gpsd, 8375 Signal K |
| `udp` | 0 | escucha NMEA por UDP (el instrumento de viento: 10110); 0 = no |
| `owmKey` | "" | clave de OpenWeatherMap: viento de internet sin la app (secreto: `********` en la WebUI) |
| `wind` | true | el viento de la red o de OpenWeatherMap va a la protección por viento (`wind.on`) |
| `pos` | true | la posición de la red sustituye a `aprs.lat` / `aprs.lon` |
| `heading` | false | rotor en un barco o un coche: acimut verdadero con el rumbo de la red |
| `headStep` | 3 | grados de cambio de rumbo para volver a apuntar (0,5-45) |
| `magVar` | 0 | declinación (grados, E positiva) si solo llega rumbo magnético (HDG / HDM) y no hay posición o `magAuto` es `false` |
| `magAuto` | true | declinación del Modelo Magnético Mundial (WMM2025) en la posición de la estación |

## `sky`

Seguir el sol o la luna (orden `track`).

| Clave | Por defecto | |
|---|---|---|
| `step` | 0.01 | grados que tiene que moverse el astro (o el satélite) para volver a apuntar, en az/el (0,01-20; por debajo de la resolución de paso del eje no afina más) |
| `minEl` | 0 | por debajo de esta elevación espera (-5 a 60) |
| `refraction` | true | suma la refracción media (lo que ve la antena) |

## `p2p`

Enlace punto a punto: apuntar a otra estación (orden `track` `p2p`). Ver [P2P.md](P2P.md).

| Clave | Valor | Para |
|---|---|---|
| `p2p.src` | `fixed` / `net` / `aprs` | de dónde sale la posición del otro extremo |
| `p2p.lat` / `lon` / `alt` | 0 / 0 / 0 | posición fija (`src=fixed`) |
| `p2p.loc` | "" | locator Maidenhead (alternativa a lat/lon) |
| `p2p.call` | "" | indicativo cuya baliza se escucha (`src=aprs`) |
| `p2p.curv` | true | tener en cuenta la curvatura terrestre en la elevación |

## `sat` (solo Raspberry Pi)

Seguimiento de satélites (orden `track` `sat`): descarga el TLE, lista los satélites y sigue el elegido con SGP4. Ver [SAT.md](SAT.md).

| Clave | Por defecto | |
|---|---|---|
| `sat.tleUrl` | `sats_all.txt` del proyecto (como la app) | fichero de satélites: formato del proyecto (`S|…`/TLE/`T|…`) o TLE plano de tres líneas |
| `sat.name` | "" | satélite recordado (nombre o número de catálogo) |
| `sat.refreshH` | 12 | cada cuántas horas se refresca el TLE y AMSAT (0 = nunca) |
| `sat.horizonH` | 8 | horizonte de la predicción de pasos (horas) |
| `sat.amsat` | true | marcar satélites activos con AMSAT Status (filtro «Activos») |
| `sat.amsatUrl` | AMSAT summary | endpoint del estado de AMSAT (`status.amsat.org`) |

La elevación mínima para los pasos y el umbral de reapuntado son los de `sky` (`sky.minEl`, `sky.step`).

## `inmarsat`

Calibración apuntando a un satélite geoestacionario Inmarsat (orden `inmarsat`), para fijar el cero de acimut / elevación con la baliza del satélite y la posición del GPS. Ver [RASPBERRY_HATS.md](RASPBERRY_HATS.md).

| Clave | Por defecto | |
|---|---|---|
| `inmarsat.lon` | 25.0 | longitud del satélite en grados (E +, O −); 25 = I-4 F2 (EMEA) |
| `inmarsat.name` | Inmarsat I-4 F2 (25°E) | nombre para mostrar |

## `pointing` y `peak`

Modelo de apuntado multipunto (corrige el desalineado mecánico) y auto-pico con el SDR. Ver [POINTING.md](POINTING.md).

| Clave | Por defecto | |
|---|---|---|
| `pointing.on` | false | aplicar el modelo a todo el apuntado |
| `pointing.ia` / `ca` / `np` / `ie` / `tf` | 0 | coeficientes (índice az, colimación, no-perpendicularidad, índice el, flexión); los calcula el ajuste |
| `pointing.points` | `[]` | referencias `[az_cielo, el_cielo, az_rotor, el_rotor]` |
| `peak.host` / `port` | "" / 0 | sonda de potencia del SDR por la red (vacío = sin auto-pico) |
| `peak.kind` / `query` | line / "" | `line` (una línea con el número) o `rigctl` (`l STRENGTH` a un rigctld); `query` a enviar |
| `peak.span` / `step` / `dwell` | 3 / 0.5 / 1 | medio lado y paso de la rejilla (grados) y segundos de espera por punto |
| `drift.decStart` / `decEnd` / `decStep` | 0 / 60 / 5 | barrido por deriva (H1): declinaciones (grados) y paso |
| `drift.dwell` | 300 | segundos fijo en cada declinación mientras el cielo cruza el haz |
| `drift.csv` | `/var/lib/aprs-lora-plus-rotor/drift.csv` | registro del apuntado del barrido (utc, dec, az, el) |

## `att` (solo Raspberry Pi)

Compensación de actitud en plataforma móvil (coche / barco) o si el mástil gira con el viento (orden `att`). Ver [ATTITUDE.md](ATTITUDE.md).

| Clave | Por defecto | |
|---|---|---|
| `att.on` | false | activa la compensación de escora / cabeceo / giro |
| `att.step` | 0.5 | umbral de reapuntado al cambiar la actitud (grados) |
| `att.staleS` | 5 | segundos tras los que la actitud se da por vieja |
| `att.rollSign` / `pitchSign` / `yawSign` | 1 | signo de cada eje (±1) para calibrar la IMU |
| `att.level` | false | nivela la escora con el 3.er eje (inclinación / cross-level) si la antena lo tiene (`stepper.x`) |
| `att.xSign` | 1 | signo del eje de inclinación (±1) para la nivelación cross-level |

## `driver`, `stepper` y `gps` (solo Raspberry Pi)

`driver`: `hamlib` (de serie; rotctld de Hamlib con el controlador del rotor) o `stepper` (motores paso a paso con el Waveshare Stepper Motor HAT). Detalles y montaje en [RASPBERRY_HATS.md](RASPBERRY_HATS.md).

`serialPorts`: lista de puertos serie donde la Pi hace de controlador (`device`, `baud`, `proto` 0-4 como `usb.proto`). `install.sh --link-uart` añade `{"device": "/dev/rotor-link", "baud": 9600, "proto": 1}` para una pantalla del rotor por cable.

| Clave | Por defecto | |
|---|---|---|
| `stepper.az` / `stepper.el` | pines del HAT | `dir`, `step`, `en`, `mode` (motor 1: 13, 19, 12, [16, 17, 20]; motor 2: 24, 18, 4, [21, 22, 27]); `stepsRev` 200, `micro` 32, `gear` 1, `dps` 4, `accel` 8, `invert`, `hold`; final de carrera `home` (-1 = no), `homeAt`, `homeDir`, `homeActive`, `homePullup`; `el.on` |
| `stepper.minAz` / `maxAz` / `minEl` / `maxEl` | 0 / 360 / 0 / 90 | topes (acimut hasta 720 con solape) |
| `stepper.enActive` | 1 | nivel de ENABLE que da corriente (1 en el HAT Rev2.1); hay `enActive` por eje (el TMC2209 del 3er eje usa 0 = activo bajo) |
| `stepper.x` | 3er eje (inclinación / cross-level), apagado de serie (`x.on`) | driver aparte TMC2209: `dir` 6, `step` 5, `en` 23 (activo bajo); `gear`, `micro`, `invert` como az/el |
| `stepper.minX` / `maxX` | -41.25 / 41.25 | topes del 3er eje |
| `stepper.antenna` | "" | perfil de antena cargado por el selector (ver [RASPBERRY_HATS.md](RASPBERRY_HATS.md#selector-de-antenas)) |
| `stepper.modePins` | false | micropasos por GPIO (solo con las resistencias de 0 Ω del reverso; si no, los DIP) |
| `stepper.az.enc` / `el.enc` / `x.enc` | apagado (`on` false) | encoder magnético AS5600 por eje: `bus` (I²C), `addr` 54 (0x36), `mux` (dirección del TCA9548A, -1 = sin mux), `muxCh` 0-7, `cpr` 4096, `sign`, `offset`, `tol` 2.0 (° de aviso), `correct` (corregir la posición al ángulo medido en vez de solo avisar) |
| `stepper.x.tmc` (o `az.tmc` / `el.tmc`) | apagado (`on` false) | TMC2209 por UART: `port`, `baud` 115200, `addr` 0-3, `irun` / `ihold` (0-31), `delay`, `sgthrs` (0-255, StallGuard), `tcoolthrs`, `spread`. El micropaso es el del eje (`micro`). Para homing sensorless, cablea DIAG a un GPIO y ponlo en `home` con `homeActive` 1 |
| `stepper.homeOnStart` | true | buscar los finales de carrera al arrancar |
| `stepper.chip` | -1 | `/dev/gpiochipN` (-1 = el del conector de 40 pines) |
| `gps.on` | false | GPS propio por gpsd: su posición manda sobre `lan.pos` y `aprs.lat` / `aprs.lon` |
| `gps.host` / `gps.port` | 127.0.0.1 / 2947 | gpsd |
| `gps.hold` | true | congela la posición con el equipo parado (quita el baile del fix) |
| `gps.holdStart` | 0.97 | m/s (de gpsd) por encima de los cuales, 3 lecturas seguidas, se da por en marcha |
| `gps.holdStop` | 0.5 | m/s por debajo de los cuales, 3 lecturas seguidas, se vuelve a parado |
| `gps.holdM` | 15 | salto en m que también da por movido cuando no hay velocidad fiable |
| `gps.ublox` | "" | static hold del receptor por UBX: `m8` (u-blox 8, Uputronics; CFG-NAV5), `m10` (MAX-M10S) y `x20` (ZED-X20P) por CFG-VALSET, "" = no tocar |
| `gps.staticHoldCms` | 20 | umbral de velocidad del static hold (cm/s, 0,01 m/s); 0 = deja los valores del receptor |
| `gps.staticHoldDistM` | 2 | distancia del static hold (m) |
| `gps.ctrlSock` / `gps.dev` | /run/gpsd.sock / /dev/serial0 | socket de control de gpsd y puerto del receptor (para el static hold) |

Con `gps.on`, ningún pin de `stepper` puede ser el GPIO18 (el PPS del Uputronics).

El filtro `hold` es el mismo del tracker APRS LoRa+ (histéresis con dos umbrales y 3 lecturas): parado devuelve la posición congelada y el apuntado no baila; en marcha (coche / barco) sigue el fix en vivo. Con `ublox`, además, el propio receptor hace su static hold: el **Uputronics GPS/RTC HAT lleva un u-blox 8 (M8030)** → `m8`; el **MAX breakout es un MAX-M10S** → `m10`; la **ZED-X20P** (alta precisión) → `x20`. Vacía `gps.ctrlSock` para no tocar el receptor.

## `gps` (receptor u-blox local, solo pantallas ESP32 con WiFi)

Alternativa a la posición por gpsd en red (`lan`): un receptor u-blox conectado al UART de un conector de la placa. Lee NMEA (RMC, GGA…) y su posición manda sobre la fija de Estación; al arrancar le manda el static hold por UBX (`rc_ubx.h`). El filtro de congelado con el equipo parado es el de `lan` (`lan.hold*`), que se aplica igual venga la posición de la red o del GPS local.

| Clave | Por defecto | |
|---|---|---|
| `gps.on` | false | receptor u-blox local por UART |
| `gps.uart` | 1 | UART de la placa (1 o 2); debe estar libre de la pasarela serie y del CAT |
| `gps.rx` / `gps.tx` | -1 / -1 | GPIO del UART (TX solo hace falta para mandarle el static hold; -1 = no configurar el chip) |
| `gps.baud` | 9600 | baudios del receptor |
| `gps.ublox` | "" | static hold por UBX: `m8` (CFG-NAV5), `m10` / `x20` (CFG-VALSET), "" = no tocar el chip |
| `gps.holdCms` | 80 | umbral de velocidad del static hold del chip (cm/s) |
| `gps.holdM` | 10 | distancia máxima del static hold del chip (m) |

El UART del GPS no puede compartir GPIO con la pasarela serie (`serial.rx/tx/de`), el CAT (`cat.rx/tx`) ni el anemómetro. En las placas con solo dos UART (ESP32-C5 / C6) uno lo usa la consola: reparte `gps.uart` entre la pasarela/CAT y el GPS según lo que tengas conectado.

## `aprs`

`call` (indicativo del rotor), `tocall` (`APZLRR`), `path`, `allow`, `totp`, `reply`, `beaconMin`, `lat`, `lon`, `comment`. Ver [APRS_CONTROL.md](APRS_CONTROL.md).
