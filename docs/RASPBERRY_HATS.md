# Raspberry Pi con Waveshare Stepper Motor HAT y Uputronics GPS/RTC HAT

La caja del rotor en una Raspberry Pi (Zero 2 W, 3B / 3B+, 4, 5) puede mover el rotor ella misma, sin controlador comercial ni Hamlib: dos motores paso a paso con el **Waveshare Stepper Motor HAT** (dos DRV8825). El **Uputronics GPS/RTC HAT** da la posición de la estación, la hora con PPS para NTPsec y un reloj RV-3028 que guarda la hora sin red.

Las dos placas se pueden usar por separado, pero **juntas chocan en el GPIO18**: ver [Los dos HAT a la vez](#los-dos-hat-a-la-vez).

Modelos: Pi 3B / 3B+, 4B / 400, 5 y Zero 2 W ([tabla](RASPBERRY_PI.md#modelos): UART del GPS, reloj, segundo UART, pasos por segundo y alimentación de cada uno).

## Instalación

```sh
cd APRS-LoRa-Plus-Rotor/raspberrypi
sudo ./install.sh --stepper --gps      # o solo una de las dos
sudo reboot                            # con --gps: UART, PPS y reloj
```

- `--stepper`: instala `python3-lgpio` (pulsos por `/dev/gpiochip`, también en la Pi 5) y pone `"driver": "stepper"`.
- `--gps`: instala `gpsd`, `gpsd-clients` y `pps-tools`; añade a `config.txt` `enable_uart=1`, `dtparam=i2c_arm=on`, `dtoverlay=pps-gpio,gpiopin=18` y `dtoverlay=i2c-rtc,rv3028,backup-switchover-mode=1` (en la Pi 5 también `dtparam=uart0=on` y `dtparam=rtc=off`: su reloj propio se apaga para que mande el RV-3028 del HAT; `PI5_RTC=1` lo deja); quita la consola del puerto serie; gpsd lee `/dev/serial0` (`/dev/ttyAMA0` en la Pi 5) y `/dev/pps0`; pone `"gps": {"on": true}`.
- `--link-uart`: UART para una pantalla del rotor por cable ([abajo](#2-pantalla-lcd-7--lcd-7b-por-cable-gs-232b)).
- Con las dos, si el STEP del motor 2 sigue en el GPIO18, lo cambia a **26** en la configuración (el cable de abajo).

Comprobar el GPS: `cgps` (posición y satélites), `ppstest /dev/pps0` (un pulso por segundo con el GPS fijado), `ntpq -p` (`GPS` y `PPS` además del ROA) y `sudo hwclock -r` (reloj del HAT).

## Waveshare Stepper Motor HAT

| | Motor 1 = acimut (A1 A2 B1 B2) | Motor 2 = elevación (A3 A4 B3 B4) |
|---|---|---|
| DIR | GPIO13 | GPIO24 |
| STEP | GPIO19 | **GPIO18** |
| ENABLE | GPIO12 | GPIO4 |
| MODE0 / 1 / 2 | GPIO16 / 17 / 20 | GPIO21 / 22 / 27 |

Pines del wiki de Waveshare (`waveshare.com/wiki/Stepper_Motor_HAT`). No usa el I2C (GPIO2 / 3) ni el UART (GPIO14 / 15).

- **Alimentación de los motores**: 8,2 a 28 V en VIN. El regulador de 5 V del HAT puede alimentar la Pi (interruptor de la placa). Una **Pi 5** necesita su propia fuente de 5 V y 5 A por USB-C: deja ese interruptor apagado.
- **Corriente**: potenciómetro de cada DRV8825. Con las resistencias de 0,2 Ω del HAT, la corriente en amperios es la tensión de referencia en voltios (Ichop = Vref). Ponla a la del motor o un poco menos.
- **Micropasos**: interruptores D0-D2 (motor 1) y D3-D5 (motor 2). De fábrica están todos a 1 = 1/32. `stepper.az.micro` y `stepper.el.micro` deben coincidir con ellos. Los pines MODE solo llegan al DRV8825 si se sueldan las resistencias de 0 Ω del reverso (`stepper.modePins`, apagado de serie). La WebUI (menú ⚙ > **Antena**) muestra, para el micropaso que tiene cada motor, la posición exacta de los DIP (cada trío es M0·M1·M2) y recuerda la alimentación y el ajuste de corriente, para no tener que mirar la tabla del datasheet.
- **ENABLE**: en las placas Rev2.1 (compradas desde enero de 2021), alto = motor con corriente (`stepper.enActive` 1). En las anteriores, prueba con 0.
- Parado, el motor se queda **sin corriente** (`hold` false) para que no se caliente. Un rotor de tornillo sin fin no se mueve solo. Si el tuyo se mueve, pon `hold: true` en ese eje.

### Mecánica (`stepper.az`, `stepper.el`)

| Clave | Por defecto | Qué es |
|---|---|---|
| `stepsRev` | 200 | pasos por vuelta del motor (1,8°) |
| `micro` | 32 | micropasos, los de los DIP |
| `gear` | 1 | vueltas del motor por una vuelta de la antena (tornillo sin fin 1:60 → 60) |
| `dps` | 4 | velocidad, grados por segundo de la antena |
| `accel` | 8 | aceleración, grados/s² (rampa al arrancar y al frenar) |
| `invert` | false | gira al revés |
| `hold` | false | corriente con el motor parado |
| `home`, `homeAt`, `homeDir`, `homeActive`, `homePullup` | -1, 0, -1, 0, true | final de carrera: pin (-1 = no hay), grados donde está, hacia dónde buscarlo, nivel cuando se pisa (0 = cierra a masa), resistencia de subida |
| `on` (`el` / `x`) | true / false | activa el eje (elevación de serie sí, inclinación de serie no; ver [Tercer eje](#tercer-eje-inclinación--cross-level-tmc2209)) |

Pasos por grado = `stepsRev × micro × gear / 360`. Por ejemplo, un NEMA 17 con tornillo sin fin 1:60 a 1/8 da 266,7 pasos por grado; a 4°/s son 1067 pasos por segundo. La configuración rechaza más de 20 000 pasos/s.

Topes: `stepper.minAz` / `maxAz` (0 / 360; hasta 720 para un rotor con solape, el acimut va por el camino más corto dentro de los topes) y `minEl` / `maxEl` (0 / 90). La caja nunca da un paso más allá de los topes. Mover a mano lleva el eje hacia el tope mientras se mantiene pulsado.

### Posición

Los motores paso a paso no dicen dónde están: la caja **cuenta los pasos**.

- La posición se guarda al parar y al apagar, en `/var/lib/aprs-lora-plus-rotor/stepper.json`, y vale al volver a arrancar. Si se corta la luz con el rotor en marcha, o el motor pierde pasos (viento, hielo, corriente baja), queda mal.
- **Corregir desfase** (orden `here`; *Calibración* de la WebUI o *Rotor > Calibration > Offset* de la app): con la antena apuntando a un rumbo conocido, escribe los grados y la posición pasa a valer eso.
- **Final de carrera** (opcional, uno por eje): por ejemplo GPIO5 para el acimut y GPIO6 para la elevación, un contacto a masa. Con `homeOnStart` (de serie), la caja lo busca al arrancar a `dps` en el sentido `homeDir` y, al pisarlo, la posición pasa a `homeAt`. La orden `home` repite la búsqueda. Si recorre todo el giro más 20° sin encontrarlo, se para y da el fallo `HOME AZ` / `HOME EL`.
- Sin posición guardada ni final de carrera, el estado es `NOPOS` hasta el primer *Corregir desfase*.

#### Encoder magnético (AS5600): detectar pasos perdidos

Para no quedarte a ciegas con el lazo abierto, puedes poner un **encoder magnético AS5600** (sensor de ángulo de 12 bits, 4096 cuentas/vuelta, I²C) en cada eje, con un imán diametral en el eje de la antena. Al parar, la caja compara el ángulo medido con la cuenta de pasos: si difieren más de `tol` grados, da el fallo **`ENCODER AZ`** / `ENCODER EL` / `ENCODER X` (pasos perdidos). Con `correct` activado, en vez de avisar **corrige** la posición al ángulo del encoder (conservando la vuelta en el acimut con solape).

El AS5600 tiene dirección fija 0x36, así que para **varios ejes** hace falta un **multiplexor I²C TCA9548A** (un canal por eje): pon `mux` a su dirección (0x70 = 112) y `muxCh` al canal. Un solo encoder va sin mux (`mux` −1). Configuración por eje en `stepper.az.enc` / `el.enc` / `x.enc` (`on`, `bus`, `addr`, `mux`, `muxCh`, `cpr`, `sign`, `offset`, `tol`, `correct`); ver [CONFIG.md](CONFIG.md). El ángulo medido sale en el evento `raw` (`azEnc`, `elEnc`, `xEnc`). Necesita `python3-smbus2` y el I²C activado (GPIO2/3).

### Tercer eje: inclinación / cross-level (TMC2209)

Algunas antenas estabilizadas (como la del domo **Satlink FB250+**) llevan un tercer eje además de acimut y elevación: la **inclinación** o *cross-level*, que mantiene la antena a nivel. De serie está apagado (`stepper.x.on` false). Es **independiente de az/el**: no entra en rotctld (Hamlib solo maneja acimut y elevación); se manda por la API / WebUI / app con la orden `axis` (`{"c":"axis","axis":"x","deg":...}`). En estación fija se aparca al centro; en móvil (vehículo o barco) sirve para nivelar con el balanceo.

El HAT Waveshare solo lleva dos DRV8825, así que el tercer eje va en un **driver aparte** (p. ej. un **TMC2209**) cableado a GPIO libres. Dos avisos:

- **ENABLE al revés**: el TMC2209 es **activo bajo** (`stepper.x.enActive` 0), al contrario que el HAT Rev2.1 (activo alto). Por eso `enActive` es por eje.
- **Micropasos**: en el TMC2209 los fijan los pines MS1 / MS2 (8 / 16 / 32 / 64). Pon `stepper.x.micro` al mismo valor. Un condensador de ~100 µF entre VM y GND evita que se queme al conectar.

Pines por defecto del tercer eje (libres, sin chocar con el HAT ni con el PPS del GPS):

| | Eje x = inclinación (TMC2209) |
|---|---|
| DIR | GPIO6 |
| STEP | GPIO5 |
| ENABLE | GPIO23 (activo bajo) |

Mecánica igual que az/el (`stepper.x`: `stepsRev`, `micro`, `gear`, `dps`, `accel`, `invert`, `hold`, `home`…) y topes propios `stepper.minX` / `maxX` (de serie ±41,25°).

#### TMC2209 por UART: corriente, micropasos y homing sensorless

En vez de fijar la corriente con el potenciómetro y los micropasos con jumpers, puedes **configurar el TMC2209 por UART**: conecta su patilla `PDN_UART` a un puerto serie de la Pi (con una resistencia de ~1 kΩ en serie para el medio dúplex) y activa `stepper.x.tmc`. La caja le fija al arrancar la corriente (`irun` / `ihold`, 0-31), los micropasos (los de `stepper.x.micro`) y **StallGuard** (`sgthrs`, `tcoolthrs`). Config: `port`, `baud` 115200, `addr` 0-3, `irun`, `ihold`, `delay`, `sgthrs`, `tcoolthrs`, `spread` (ver [CONFIG.md](CONFIG.md)); necesita `python3-serial`.

**Homing sensorless** (sin final de carrera): con StallGuard, el TMC2209 pone su salida **DIAG** a 1 cuando el motor se bloquea (al topar). Cablea DIAG a un GPIO libre y pon ese pin en `stepper.x.home` con `homeActive` 1: el `home` normal lleva el eje contra el tope y el stall hace de final de carrera. Afina `sgthrs` para que dispare al topar pero no en marcha normal: en la WebUI, menú ⚙ > **Antena** > *Leer StallGuard* (o la orden `sg`) muestra el `SG_RESULT` de cada eje mientras mueves.

#### Nivelación automática con el balanceo (cross-level)

En móvil (vehículo o barco), el eje de inclinación puede **nivelar solo** con la escora. Enciende la compensación de actitud (`att.on`) con una IMU que dé escora y cabeceo (orden `att`, o la que mande la app / el tracker), y activa `att.level`. Con ello, el eje X gira para **cancelar la escora** (el balanceo) hasta su tope, y el acimut / elevación afinan el resto de la inclinación; así la antena sigue apuntando al objetivo aunque la base se mueva. `att.xSign` (−1 / 1) calibra el sentido del eje si nivela al revés. En la WebUI, cuando está nivelando, la línea de actitud lo indica («nivelando (cross-level)»). En estación fija deja `att.level` apagado: el eje se aparca al centro. (Requiere una antena con tercer eje, `stepper.x.on`.)

#### Ejemplo: domo Satlink FB250+ (tren por correa GT2, piñón de 12 dientes)

Reducción `gear` = dientes del plato / 12. Con los DIP del HAT a 1/16 (`micro` 16; D0-D2 y D3-D5 = 0·0·1) y el TMC2209 del tercer eje también a 1/16:

| Eje | Plato | `gear` | Topes |
|---|---|---|---|
| Acimut (`az`) | 142 (correa; por confirmar el conteo) | 11.833 | 0–360° |
| Elevación (`el`) | 60 | 5.0 | 0–90° |
| Inclinación (`x`, TMC2209) | 72 | 6.0 | ±41,25° (`minX` −41.25, `maxX` 41.25) |

Salen 105,2 / 44,4 / 53,3 pasos por grado; el Zero 2 W va sobrado a 4°/s. El eje x barre 82,5° de extremo a extremo (33 mm de correa). Son los valores del perfil `fb250plus`; el conteo de la corona de acimut está por confirmar, así que **calibra la ganancia** antes de fiarte del apuntado.

#### Calibrar la ganancia (dos puntos)

Si no sabes la reducción exacta (dientes sin contar, correa que patina), mídela: en la WebUI, menú ⚙ > **Antena** > *Calibrar ganancia*. Lleva el eje a una referencia y pulsa **Marcar punto A**; muévelo a mano con las flechas a otra referencia conocida, escribe los **grados reales** que ha girado y pulsa **Calcular**. Propone la `gear` real (= pasos recorridos ÷ grados reales × 360 ÷ micropasos ÷ pasos de vuelta); **Aplicar** la guarda en la configuración y reinicia. Por la app o el protocolo JSON es la orden `calstep` (`mark`, luego `deg` y opcional `apply`).

#### Fijar el cero con la baliza Inmarsat

El domo FB250+ es un terminal **FleetBroadband de Inmarsat**: su baliza es una referencia de puntería estupenda. En la WebUI, menú ⚙ > **Antena** > *Calibrar con Inmarsat*, escribe la **longitud del satélite** (grados, E +, O −; 25 = I-4 F2, el de EMEA) y pulsa **Apuntar**: con la posición del GPS calcula el az/el geoestacionario y lleva la antena ahí. Afina a mano (flechas) hasta el **máximo de señal** de la baliza en el receptor y pulsa **Fijar aquí**: el cero de acimut y elevación se corrige a los grados calculados, así que a partir de ahí el apuntado es absoluto. Necesita posición GPS. Por la app o el protocolo es la orden `inmarsat` (`lon`, `fix`); el satélite por defecto se guarda en `inmarsat.lon`.

## Selector de antenas

La misma caja (Raspberry + Waveshare Stepper Motor HAT) mueve **distintas antenas** con solo elegir su perfil. Cada antena es un archivo JSON con sus datos mecánicos —ejes, reducción `gear`, micropasos, topes, inversión y `enActive`— en un bloque `stepper` que se **superpone** sobre la configuración. Los pines del HAT (y los del TMC2209 del tercer eje) son fijos, así que no van en el perfil; solo cambian los datos de cada antena.

- **De serie**: `fb250plus` (domo Satlink FB250+, 3 ejes) y `generic-2axis` (plantilla az/el). En `aprs_lora_plus_rotor/antennas/` del paquete.
- **Propias**: pon tus `.json` en `/etc/aprs-lora-plus-rotor/antennas/` (con el mismo nombre que uno de serie, el tuyo manda; sobreviven a la reinstalación).
- **Elegir**: orden `antenna` de la API / WebUI / app (`{"c":"antenna","name":"fb250plus"}`). Carga el perfil sobre `stepper`, pasa `driver` a `stepper`, guarda y reinicia. La orden `antennas` devuelve la lista para el selector; el perfil cargado queda en `stepper.antenna`.

Un perfil (`fb250plus.json`):

```json
{
  "title": "Satlink FB250+",
  "desc": "Domo FB250+, 3 ejes, correa GT2, piñón 12T.",
  "stepper": {
    "minAz": 0, "maxAz": 360, "minEl": 0, "maxEl": 90, "minX": -41.25, "maxX": 41.25,
    "az": {"on": true, "gear": 12.25, "micro": 32, "enActive": 1},
    "el": {"on": true, "gear": 5.0,  "micro": 32, "enActive": 1},
    "x":  {"on": true, "gear": 6.0,  "micro": 32, "enActive": 0}
  }
}
```

Para añadir una antena: copia uno de serie, cambia `title`, `desc`, los `gear` (dientes del plato / dientes del piñón), los topes y `micro`, y activa (`on`) los ejes que tenga. `install.sh` copia la carpeta `antennas/` del paquete.

## Uputronics GPS/RTC HAT

| Pin de la Pi | Uso |
|---|---|
| 3 / 5 (GPIO2 / 3) | I2C: reloj RV-3028 (0x52) y GPS |
| 8 / 10 (GPIO14 / 15) | UART del u-blox (9600 baudios) |
| 12 (**GPIO18**) | PPS (*time pulse*) |
| 2 / 4 | 5 V para los supercondensadores |

Datos de la hoja de Uputronics (rev. 2.1). Necesita antena GPS por SMA.

- **Posición de la estación**: con `gps.on`, la caja lee gpsd (`gps.host` / `gps.port`, 127.0.0.1:2947). Con posición (2D o 3D, de menos de 10 s), esa manda sobre la de la red local (`lan.pos`) y sobre la configurada (`aprs.lat` / `aprs.lon`). Se usa para la baliza APRS, el seguimiento del sol y la luna, la declinación del WMM y el tiempo de OpenWeatherMap. El evento `info` lleva `gps`: `fix`, satélites usados y vistos, `pps`, `lat`, `lon`, `alt`, `held`.
- **Posición fija con el equipo parado** (`gps.hold`, de serie encendido): el fix de un GPS baila unos metros aunque la antena no se mueva, y eso haría bailar el apuntado del sol, la luna o un satélite. El filtro congela la posición mientras el equipo está parado y sigue el fix en vivo solo cuando se mueve de verdad (coche / barco). Es el mismo filtro de velocidad del tracker APRS LoRa+ (`gps_extras.cpp`): histéresis con dos umbrales y 3 lecturas seguidas, `gps.holdStart` (0,97 m/s) para darlo por en marcha y `gps.holdStop` (0,5 m/s) para volver a parado; un salto mayor que `gps.holdM` (15 m) sin velocidad fiable también reubica la referencia. En la tarjeta GPS de la WebUI sale «fijo» cuando está congelada.
- **Static hold en el receptor u-blox** (`gps.ublox`): además del filtro por software, la caja puede mandar al receptor su propio static hold por UBX (igual que el tracker, `configureStaticHold`). Según el receptor:
  - **`m8`** — u-blox 8, el del **Uputronics GPS/RTC HAT** (un M8030): **UBX-CFG-NAV5** con solo el static hold (interfaz antigua por mensajes).
  - **`m10`** — **MAX-M10S** (el uBLOX MAX breakout) y **`x20`** — **ZED-X20P** de alta precisión: **UBX-CFG-VALSET** (base de datos de configuración, claves `CFG-MOT-GNSSSPEED_THRS` y `CFG-MOT-GNSSDIST_THRS`).

  `gps.staticHoldCms` = umbral de velocidad en cm/s (0,01 m/s) y `gps.staticHoldDistM` = distancia en m; 0 = deja los valores del receptor. Se envía por el socket de control de gpsd (`gps.ctrlSock`, de serie `/run/gpsd.sock`; `gps.dev` = puerto del receptor). Es best-effort: si gpsd no tiene socket de control o no es un u-blox, se queda solo el filtro por software. Los tres HAT usan los mismos pines (UART `/dev/serial0` en GPIO14/15, I2C en GPIO2/3, PPS en GPIO18); gpsd autodetecta los baudios (9600 el MAX-M10S, 38400 la ZED-X20P, 115200 el Uputronics).

  **Aviso con la ZED-X20P y el Waveshare Stepper Motor HAT**: la X20 tiene pines opcionales que chocan con los motores: el segundo UART en GPIO12/13 (necesita `dtoverlay=uart5`) pisa el ENABLE/DIR del motor 1, y el TX-READY en GPIO5/23 pisa el STEP/ENABLE del tercer eje (TMC2209). Vienen sin conectar de fábrica: no actives `uart5` ni cablees el TX-READY. El PPS en GPIO18 choca con el STEP del motor 2, como con los otros HAT (ver el aviso de arriba; se arregla moviendo ese STEP a GPIO26).
- **Hora**: la de NTPsec (`install.sh` sin `NO_NTP`). gpsd deja la hora del NMEA en la memoria compartida 0 y la del PPS en la 1 (`raspberrypi/ntpsec.conf`). El RV-3028 guarda la hora con la Pi apagada (supercondensadores).

## Los dos HAT a la vez

El STEP del motor 2 del Stepper Motor HAT y el PPS del GPS son **los dos el GPIO18** (pin 12). Con las dos placas sin tocar, el PPS daría pasos falsos al motor de elevación y el PPS no llegaría a la Pi. Hay tres salidas:

1. **Solo acimut**: `stepper.el.on: false`. El motor 2 no se usa y el pin 12 queda para el PPS.
2. **Llevar el STEP del motor 2 a otro pin** (lo que pone `install.sh`): que el pin 12 del Stepper HAT **no** llegue a la Pi (doblado o cortado en el conector apilable, o un cable de 40 hilos con ese hilo cortado) y un cable del pin 12 del Stepper HAT al **pin 37 de la Pi (GPIO26)**. En la configuración, `stepper.el.step: 26`.
3. Quitar el PPS: el GPS sigue dando posición y hora del NMEA (unos 100 ms de error), pero sin la precisión del PPS.

Con `gps.on` y un STEP, DIR, ENABLE o final de carrera en el GPIO18, la configuración da error.

Pines que quedan libres con los dos HAT y el cambio a GPIO26: GPIO5, 6, 7, 8, 9, 10, 11, 23 y 25 (finales de carrera, anemómetro...; GPIO8 y 9 son los de la pantalla por cable, `--link-uart`).

## Emparejar la Pi con una pantalla o con la app

La Pi con los HAT no tiene pantalla. Se puede manejar a la vez desde una **pantalla del rotor** (Waveshare LCD-7 o LCD-7B con este firmware), por WiFi o por cable, y desde la **app Android** por Bluetooth. Todos ven la misma posición y cualquiera puede mover o parar.

### 1. Pantalla LCD-7 / LCD-7B por WiFi (`driver` 2)

La pantalla es cliente del **rotctld de la Pi** (puerto 4533, de serie): no hay que tocar nada en la Pi.

1. Pi y pantalla en la misma red: la de casa (`wifi.ssid` en la pantalla) o el punto de acceso de la pantalla (la Pi entra en él como en cualquier WiFi). Mejor con la **IP de la Pi fija** (reserva en el router).
2. En la pantalla (WebUI → Configuración, o la app):

```json
"driver": 2,
"remote": {"host": "192.168.1.50", "port": 4533}
```

Al conectar, la pantalla pide `\dump_state` y toma los límites de la Pi (`min_az`, `max_az`, `max_el`; hasta entonces usa `serial.minAz` / `maxAz` / `maxEl`). Lee la posición cada `serial.pollMs` (500 ms) y manda `P az el`, `M` (mover a mano), `S` (parar) y `K` (aparcar). Sin respuesta en 3 s la pantalla da el fallo **LINK** y vuelve a conectar sola (cada 3 s, hasta 15 s si la Pi no contesta). Funciona igual con cualquier rotctld de Hamlib.

El rotctld no tiene contraseña (como el de Hamlib): solo en una red de confianza.

### 2. Pantalla LCD-7 / LCD-7B por cable (GS-232B)

El UART de los pines 8 / 10 es el del GPS, así que la pantalla va por **otro UART en GPIO8 (TX) y GPIO9 (RX)**, libres con los dos HAT:

```sh
sudo ./install.sh --link-uart          # se puede juntar con --stepper --gps
sudo reboot
```

- **Pi 4**: `dtoverlay=uart4`. **Pi 5**: `dtoverlay=uart3-pi5`. Las dos en GPIO8 / GPIO9. `install.sh` crea `/dev/rotor-link` (regla de udev: el número de `ttyAMA` cambia según los UART activados) y añade a `serialPorts` `{"device": "/dev/rotor-link", "baud": 9600, "proto": 1}`: la Pi contesta como un controlador **GS-232B**.
- **Pi 3 y Zero 2 W**: no tienen otro UART en el conector (el segundo es el del Bluetooth). Usa un adaptador **USB-serie de 3,3 V**: `install.sh` pone `/dev/ttyUSB0`.

| Pantalla (conector UART) | Raspberry Pi |
|---|---|
| TX (GPIO43) | pin 21 (GPIO9, RX) |
| RX (GPIO44) | pin 24 (GPIO8, TX) |
| GND | pin 25 (GND) |

Las dos son de 3,3 V: unión directa, sin el hilo de 3V3. Cable corto (un par de metros); para más distancia, WiFi. Pon el conmutador de la pantalla en *UART2* (el conector). En la pantalla: `driver` 1, `serial.proto` 1 (GS-232B), `serial.baud` 9600, `serial.rx` 44 / `tx` 43 (los de serie en la LCD-7) y `serial.maxAz` / `maxEl` iguales a los de `stepper.az` / `stepper.el`.

Con `--link-uart`, GPIO8 y GPIO9 dejan de estar libres (y el SPI0 no se puede usar).

### 3. App Android por Bluetooth

El BLE viene encendido (`bt.ble: true`). En la app, pestaña **Rotor** → buscar → la Pi (su `name`, `APRS-Rotor` de serie). Android pide **vincular** sin número (emparejamiento «Just Works»: la Pi no tiene pantalla); el control lo protege el **PIN de la caja** (`pin`, 123456 de serie), que la app manda con `auth`. También por WiFi: TCP 4534.

- No pongas `dtoverlay=disable-bt`: quita el Bluetooth. En la Pi 3 y la 4 el GPS va por el mini UART (`/dev/serial0`) y el Bluetooth sigue con su UART; en la Pi 5 el Bluetooth va aparte.
- Olvidar un móvil: en Android, *Bluetooth → APRS-Rotor → Olvidar*, y en la Pi `bluetoothctl devices` y `bluetoothctl remove <MAC>`.

## Sin probar

Escrito con los datos de Waveshare y de Uputronics. La pantalla por WiFi probada en el PC contra el rotctld de la Pi (`\dump_state`, `p`, `P`, `M`, `S`, `K`). Probado en el PC con un GPIO simulado (rampas, topes, solape, finales de carrera, posición guardada) y con mensajes de gpsd de ejemplo. **No se ha probado con las placas, motores ni un GPS de verdad.**
