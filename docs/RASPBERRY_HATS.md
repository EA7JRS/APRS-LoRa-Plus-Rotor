# Raspberry Pi con Waveshare Stepper Motor HAT y Uputronics GPS/RTC HAT

La caja del rotor en una Raspberry Pi (Zero 2 W, 3, 4, 5) puede mover el rotor ella misma, sin controlador comercial ni Hamlib: dos motores paso a paso con el **Waveshare Stepper Motor HAT** (dos DRV8825). El **Uputronics GPS/RTC HAT** da la posición de la estación, la hora con PPS para NTPsec y un reloj RV-3028 que guarda la hora sin red.

Las dos placas se pueden usar por separado, pero **juntas chocan en el GPIO18**: ver [Los dos HAT a la vez](#los-dos-hat-a-la-vez).

## Instalación

```sh
cd APRS-LoRa-Plus-Rotor/raspberrypi
sudo ./install.sh --stepper --gps      # o solo una de las dos
sudo reboot                            # con --gps: UART, PPS y reloj
```

- `--stepper`: instala `python3-lgpio` (pulsos por `/dev/gpiochip`, también en la Pi 5) y pone `"driver": "stepper"`.
- `--gps`: instala `gpsd`, `gpsd-clients` y `pps-tools`; añade a `config.txt` `enable_uart=1`, `dtparam=i2c_arm=on`, `dtoverlay=pps-gpio,gpiopin=18` y `dtoverlay=i2c-rtc,rv3028,backup-switchover-mode=1` (en la Pi 5 también `dtparam=uart0=on`); quita la consola del puerto serie; gpsd lee `/dev/serial0` (`/dev/ttyAMA0` en la Pi 5) y `/dev/pps0`; pone `"gps": {"on": true}`.
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
- **Micropasos**: interruptores D0-D2 (motor 1) y D3-D5 (motor 2). De fábrica están todos a 1 = 1/32. `stepper.az.micro` y `stepper.el.micro` deben coincidir con ellos. Los pines MODE solo llegan al DRV8825 si se sueldan las resistencias de 0 Ω del reverso (`stepper.modePins`, apagado de serie).
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
| `on` (solo `el`) | true | sin elevación: false |

Pasos por grado = `stepsRev × micro × gear / 360`. Por ejemplo, un NEMA 17 con tornillo sin fin 1:60 a 1/8 da 266,7 pasos por grado; a 4°/s son 1067 pasos por segundo. La configuración rechaza más de 20 000 pasos/s.

Topes: `stepper.minAz` / `maxAz` (0 / 360; hasta 720 para un rotor con solape, el acimut va por el camino más corto dentro de los topes) y `minEl` / `maxEl` (0 / 90). La caja nunca da un paso más allá de los topes. Mover a mano lleva el eje hacia el tope mientras se mantiene pulsado.

### Posición

Los motores paso a paso no dicen dónde están: la caja **cuenta los pasos**.

- La posición se guarda al parar y al apagar, en `/var/lib/aprs-lora-plus-rotor/stepper.json`, y vale al volver a arrancar. Si se corta la luz con el rotor en marcha, o el motor pierde pasos (viento, hielo, corriente baja), queda mal.
- **Corregir desfase** (orden `here`; *Calibración* de la WebUI o *Rotor > Calibration > Offset* de la app): con la antena apuntando a un rumbo conocido, escribe los grados y la posición pasa a valer eso.
- **Final de carrera** (opcional, uno por eje): por ejemplo GPIO5 para el acimut y GPIO6 para la elevación, un contacto a masa. Con `homeOnStart` (de serie), la caja lo busca al arrancar a `dps` en el sentido `homeDir` y, al pisarlo, la posición pasa a `homeAt`. La orden `home` repite la búsqueda. Si recorre todo el giro más 20° sin encontrarlo, se para y da el fallo `HOME AZ` / `HOME EL`.
- Sin posición guardada ni final de carrera, el estado es `NOPOS` hasta el primer *Corregir desfase*.

## Uputronics GPS/RTC HAT

| Pin de la Pi | Uso |
|---|---|
| 3 / 5 (GPIO2 / 3) | I2C: reloj RV-3028 (0x52) y GPS |
| 8 / 10 (GPIO14 / 15) | UART del u-blox (9600 baudios) |
| 12 (**GPIO18**) | PPS (*time pulse*) |
| 2 / 4 | 5 V para los supercondensadores |

Datos de la hoja de Uputronics (rev. 2.1). Necesita antena GPS por SMA.

- **Posición de la estación**: con `gps.on`, la caja lee gpsd (`gps.host` / `gps.port`, 127.0.0.1:2947). Con posición (2D o 3D, de menos de 10 s), esa manda sobre la de la red local (`lan.pos`) y sobre la configurada (`aprs.lat` / `aprs.lon`). Se usa para la baliza APRS, el seguimiento del sol y la luna, la declinación del WMM y el tiempo de OpenWeatherMap. El evento `info` lleva `gps`: `fix`, satélites usados y vistos, `pps`, `lat`, `lon`, `alt`.
- **Hora**: la de NTPsec (`install.sh` sin `NO_NTP`). gpsd deja la hora del NMEA en la memoria compartida 0 y la del PPS en la 1 (`raspberrypi/ntpsec.conf`). El RV-3028 guarda la hora con la Pi apagada (supercondensadores).

## Los dos HAT a la vez

El STEP del motor 2 del Stepper Motor HAT y el PPS del GPS son **los dos el GPIO18** (pin 12). Con las dos placas sin tocar, el PPS daría pasos falsos al motor de elevación y el PPS no llegaría a la Pi. Hay tres salidas:

1. **Solo acimut**: `stepper.el.on: false`. El motor 2 no se usa y el pin 12 queda para el PPS.
2. **Llevar el STEP del motor 2 a otro pin** (lo que pone `install.sh`): que el pin 12 del Stepper HAT **no** llegue a la Pi (doblado o cortado en el conector apilable, o un cable de 40 hilos con ese hilo cortado) y un cable del pin 12 del Stepper HAT al **pin 37 de la Pi (GPIO26)**. En la configuración, `stepper.el.step: 26`.
3. Quitar el PPS: el GPS sigue dando posición y hora del NMEA (unos 100 ms de error), pero sin la precisión del PPS.

Con `gps.on` y un STEP, DIR, ENABLE o final de carrera en el GPIO18, la configuración da error.

Pines que quedan libres con los dos HAT y el cambio a GPIO26: GPIO5, 6, 7, 8, 9, 10, 11, 23 y 25 (finales de carrera, anemómetro...).

## Sin probar

Escrito con los datos de Waveshare y de Uputronics. Probado en el PC con un GPIO simulado (rampas, topes, solape, finales de carrera, posición guardada) y con mensajes de gpsd de ejemplo. **No se ha probado con las placas, motores ni un GPS de verdad.**
