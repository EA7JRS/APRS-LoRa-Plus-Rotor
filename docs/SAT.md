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
- **WebUI** → tarjeta *Mando* → **🛰 SAT**: abre el selector con la lista de satélites, cada uno
  con su próximo paso (hora UTC y culminación) y los visibles ahora arriba del todo. Hay un
  **buscador**, **filtros por categoría** (en orden alfabético y solo las que existen en los
  datos: Amateur, CubeSats, Educación, Estaciones, Geo, Ingeniería, Inmarsat, Iridium, LEO, LoRa,
  Meteo, SatNOGS, Visuales, Ciencia) y un filtro **Activos** (los que AMSAT ha oído en las últimas
  24 h, con un punto verde). Pulsar un satélite y el rotor empieza a seguirlo. La tarjeta
  *Objetivo* muestra `🛰 nombre`, az/el, culminación máxima, LOS y distancia.
- **Actualizar TLE**: botón del propio selector (descarga en segundo plano) o automático
  (ver `sat.refreshH`); también refresca el estado de AMSAT.
- **Órdenes** (API JSON / app): `{"c":"sats"}` lista (con `grp` = letra de categoría y `act` =
  solo activos); `{"c":"track","t":"sat","id":"ISS (ZARYA)"}` elige y sigue (el `id` es el nombre
  o el número de catálogo); `{"c":"track","t":"off"}` para; `{"c":"tle"}` fuerza la descarga.
  Cualquier orden de mover, parar o aparcar deja de seguir.

## Datos de satélites (lo mismo que la app Android)
- Fuente por defecto: el **fichero agregado del proyecto** (rama `satdata` de `APRS-LoRa-Plus`,
  generado a diario por el workflow `satdata.yml`), **la misma que usa la app Android**:
  `https://raw.githubusercontent.com/EA7JRS/APRS-LoRa-Plus/satdata/sats_all.txt`. Junta muchos
  grupos de CelesTrak (estaciones, amateur, CubeSats, SatNOGS, meteo, visuales, ciencia,
  educación, ingeniería, LoRa/TinyGS, geoestacionarios) y añade transpondedores (SatNOGS DB).
  Configurable en `sat.tleUrl`; también entiende un **TLE plano** de tres líneas (p. ej. un grupo
  de CelesTrak) si prefieres otra fuente.
- Formato propio: líneas `S|norad|nombre|cat|grupos`, las dos de TLE y líneas `T|…`
  (transpondedores, que el rotor ignora de momento; el apuntado solo necesita el TLE). La
  **categoría** de cada satélite (letra en `grupos`) alimenta el filtro del selector. Además, el
  rotor deriva **Inmarsat** e **Iridium** del nombre y **LEO** del movimiento medio del TLE, y el
  selector solo muestra las categorías que existen en los datos cargados, en orden alfabético.
- Caché en `/var/lib/aprs-lora-plus-rotor/sats.txt`; se refresca cada `sat.refreshH` horas
  (0 = nunca) y al arrancar si falta o está viejo. El satélite elegido se recuerda en `sat.name`
  y se vuelve a enlazar tras cada actualización.

## Satélites activos (AMSAT)
- `sat.amsat` (de serie sí) descarga el **AMSAT OSCAR Satellite Status**
  (`https://status.amsat.org/api/v1/summary.php`, configurable en `sat.amsatUrl`): los satélites
  que la gente ha reportado oídos en las **últimas 24 h** (todo menos «Not Heard»).
- El casado con el TLE es por **designador** (AO-91, IO-117, RS-44…), ignorando guiones, ceros,
  mayúsculas y el sufijo de modo, como hace la propia AMSAT. Algún satélite con nombre común
  distinto del designador puede no casar (p. ej. Es'hail-2 / QO-100 si el TLE no lleva «QO-100»).
- En el selector, el filtro **Activos** deja solo esos, y cada activo lleva un punto verde.

## Posición propia
Igual que sol/luna y P2P: del **GPS** (gpsd), de la **red** (`lan.pos`: barco o coche) o de la
config (`aprs.lat` / `aprs.lon`). Sin posición, el selector avisa y no calcula pasos.

## Config (`sat`)
| Clave | De serie | Qué hace |
|---|---|---|
| `tleUrl` | `sats_all.txt` del proyecto | fichero de satélites (formato del proyecto o TLE plano) |
| `name` | (vacío) | satélite recordado (nombre o número de catálogo) |
| `refreshH` | 12 | cada cuántas horas se refresca el TLE y AMSAT (0 = nunca) |
| `horizonH` | 8 | horizonte de la predicción de pasos (h) |
| `amsat` | true | marcar satélites activos con AMSAT Status |
| `amsatUrl` | AMSAT summary | endpoint del estado de AMSAT |

La elevación mínima para considerar un paso y para esperar «bajo el horizonte» es `sky.minEl`
(la misma que sol/luna). El umbral de reapuntado es `sky.step`. En modo satélite el seguimiento
recalcula y re-apunta a **10 Hz** (cada 0,1 s; los LEO se mueven rápido), frente a 1 s en sol/luna/P2P.
Si los motores aún se quedan atrás cerca del cenit (paso alto), sube la velocidad y la aceleración
del eje (`stepper.<eje>.dps` y `.accel`).

## Cálculo
SGP4 en Python puro por el subpaquete **`sgp4/`** (copia vendorizada de
[python-sgp4](https://github.com/brandon-rhodes/python-sgp4) de Brandon Rhodes, licencia MIT;
ver `sgp4/LICENSE` y `sgp4/ORIGEN.txt`). Sin dependencias externas en tiempo de ejecución: el
demonio sigue siendo solo biblioteca estándar (+ `dbus-next` para BLE/SPP). La posición del
satélite se propaga en el marco TEME, se pasa a ECEF con la hora sideral (`gstime`) y se sacan
azimut / elevación / distancia en el marco local ENU desde la posición de la estación (WGS84).

Sin probar con el rotor en la placa.
