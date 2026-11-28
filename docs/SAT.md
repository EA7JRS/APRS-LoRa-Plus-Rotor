<!--
Copyright (C) 2026 Manuel - EA7JRS
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Seguimiento de satélites (SAT)

Seguir un satélite de órbita baja (o geoestacionario) con el rotor: la caja descarga el
TLE, calcula az/el con SGP4 desde la posición propia y apunta la antena, mostrando el paso
en curso (culminación, LOS, distancia). Pensado para la antena del domo **Satlink FB250+**
en banda L y para cualquier antena de satélite sobre el rotor.

**Solo en la Raspberry Pi** (tiene internet para el TLE y disco para la caché). En las placas
ESP32 el modo SAT no está (de momento); el seguimiento sol/luna y P2P sí.

## Cómo se usa
- **WebUI** → tarjeta *Mando* → **🛰 SAT**: abre el selector con la lista de satélites del TLE,
  cada uno con su próximo paso (hora UTC y culminación) y los visibles ahora arriba del todo.
  Buscar por nombre, pulsar uno y el rotor empieza a seguirlo. La tarjeta *Objetivo en
  seguimiento* muestra `🛰 nombre`, az/el, culminación máxima, LOS y distancia.
- **Actualizar TLE**: botón del propio selector (descarga en segundo plano) o automático
  (ver `sat.refreshH`).
- **Órdenes** (API JSON / app): `{"c":"sats"}` lista; `{"c":"track","t":"sat","id":"ISS (ZARYA)"}`
  elige y sigue (el `id` es el nombre o el número de catálogo); `{"c":"track","t":"off"}` para;
  `{"c":"tle"}` fuerza la descarga. Cualquier orden de mover, parar o aparcar deja de seguir.

## TLE
- Fuente por defecto: grupo **amateur de Celestrak**
  (`https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle`), configurable en
  `sat.tleUrl` (cualquier URL con formato TLE de tres líneas).
- Caché en `/var/lib/aprs-lora-plus-rotor/amateur.tle`; se refresca cada `sat.refreshH` horas
  (0 = nunca) y al arrancar si falta o está viejo.
- El satélite elegido se recuerda en `sat.name` y se vuelve a enlazar al TLE nuevo tras cada
  actualización.

## Posición propia
Igual que sol/luna y P2P: del **GPS** (gpsd), de la **red** (`lan.pos`: barco o coche) o de la
config (`aprs.lat` / `aprs.lon`). Sin posición, el selector avisa y no calcula pasos.

## Config (`sat`)
| Clave | De serie | Qué hace |
|---|---|---|
| `tleUrl` | Celestrak amateur | URL del fichero TLE |
| `name` | (vacío) | satélite recordado (nombre o número de catálogo) |
| `refreshH` | 12 | cada cuántas horas se refresca el TLE (0 = nunca) |
| `horizonH` | 8 | horizonte de la predicción de pasos (h) |

La elevación mínima para considerar un paso y para esperar «bajo el horizonte» es `sky.minEl`
(la misma que sol/luna). El umbral de reapuntado es `sky.step`.

## Cálculo
SGP4 en Python puro por el subpaquete **`sgp4/`** (copia vendorizada de
[python-sgp4](https://github.com/brandon-rhodes/python-sgp4) de Brandon Rhodes, licencia MIT;
ver `sgp4/LICENSE` y `sgp4/ORIGEN.txt`). Sin dependencias externas en tiempo de ejecución: el
demonio sigue siendo solo biblioteca estándar (+ `dbus-next` para BLE/SPP). La posición del
satélite se propaga en el marco TEME, se pasa a ECEF con la hora sideral (`gstime`) y se sacan
azimut / elevación / distancia en el marco local ENU desde la posición de la estación (WGS84).

Sin probar con el rotor en la placa.
