# Protocolo de control JSON (v1)

Mismo protocolo en todos los equipos (ESP32, ESP32-S3, Pico 2 W y Raspberry Pi) y por todos los transportes:

| Transporte | Dónde | Notas |
|---|---|---|
| BLE | servicio `4c6f5261-2b00-4ea7-b000-000000000001` | Órdenes: escribir en `…0002` (con o sin respuesta). Eventos: notificaciones de `…0003`. Enlace cifrado (emparejamiento «Just Works» con vinculación). |
| TCP | puerto **4534** | Una línea JSON por orden, respuestas y eventos terminados en `\n`. |
| HTTP | `POST /api` (WebUI) | Varias órdenes, una por línea; la respuesta es un array JSON. Autenticación HTTP básica (usuario `admin`, contraseña = PIN o la de la WebUI). |

Las líneas pueden llegar troceadas (BLE con MTU pequeño): se juntan hasta el `\n`.

Los UUID siguen el esquema del tracker APRS LoRa+ (`4c6f5261-2b00-4ea7-a000-…` es el servicio de control del tracker; el rotor usa `…-b000-…`).

## Autorización

Si el equipo tiene PIN (por defecto `123456`), las órdenes que mueven o configuran exigen antes:

```json
{"c":"auth","pin":"123456"}          →  {"e":"ok","c":"auth"}   |   {"e":"err","c":"auth","m":"bad pin"}
```

Sin PIN previo responden `{"e":"err","c":"pos","m":"auth required"}`. Sin exigir PIN: `info`, `get`, `raw`, `sub`.
El PIN viaja en claro dentro del enlace: el BLE va cifrado; por TCP/HTTP úsalo solo en una red de confianza.

## Órdenes

| Orden | Campos | Respuesta |
|---|---|---|
| `info` | — | evento `info` |
| `get` | — | evento `pos` |
| `sub` | `ms` (200-…; 0 = apagar) | `ok`. Desde entonces el equipo manda `pos` cada `ms` mientras se mueve y cada 5 s parado, y al empezar / terminar de moverse |
| `pos` | `az`, `el` (opcional) | `ok` / `err` (`az`, `rejected`). El acimut puede ser 0-360 o mecánico (p. ej. 400 en un rotor de 450°); la elevación se recorta a sus límites |
| `stop` | — | `ok` |
| `park` | — | `ok` / `err` |
| `move` | `d`: `cw`, `ccw`, `up`, `down` (o el número `ROT_MOVE_*` de Hamlib); `s`: velocidad 1-100 (opcional) | `ok`. Se mueve hasta `stop` o hasta el tope |
| `clear` | — | `ok`: borra el fallo (atasco, sensor) y libera el bloqueo por viento hasta la siguiente racha fuerte |
| `cal` | `axis` (`az`/`el`), `pt` (`a`/`b`), `deg` | `ok` / `err`. Toma la lectura actual del sensor como punto de calibración (solo control directo) |
| `here` | `axis`, `deg` | `ok` / `err`. Corrige el desfase: la posición actual pasa a valer `deg` (solo control directo) |
| `raw` | — | evento `raw` |
| `cfgget` | — | eventos `cfg` con la configuración entera en trozos |
| `cfgset` | `i`, `n`, `d` | `ok` por trozo; con el último, `{"e":"ok","c":"cfgset","i":…,"done":true}` y el equipo se reinicia para aplicarla |
| `cat` | — | evento `cat` (sin PIN) o `err` `no cat` |
| `catset` | `f` (Hz), `m` (modo de Hamlib: LSB, USB, CW, CWR, AM, FM, WFM, PKTUSB…), `txf` (Hz de subida), `split` (bool), `vfo` (`A`, `B`, `swap`), `tone` (CTCSS en Hz, 0 = apagado), `ptt` (bool); los que se quieran | `ok` / `err` (`no cat`, `rejected`, `ptt not allowed`) |
| `cattrack` | `on` (bool: seguir o no) y/o `name`, `dl`, `ul` (Hz nominales de bajada y subida, 0 = no), `rr` (velocidad radial, km/s, + = se aleja) | `ok`. La caja corrige el Doppler y sintoniza; sin datos en 15 s deja de seguir |
| `wx` | `ws` (viento medio, m/s), `wg` (racha, m/s, opcional), `wd` (grados, opcional), `age` (s desde la observación), `src` (`OWM`, `IQAir`…) | `ok` / `err` (`no wind`, `ws`, `range`). Tiempo de internet de la app. Con `wind.on` y racha (o viento medio sin racha) ≥ `wind.park` m/s el rotor para, va a *park* y rechaza `pos` / `move` (y los de rotctld, GS-232, APRS, pantalla…) con `rejected` hasta que el viento baja de `wind.resume` y pasan `wind.hold` minutos sin rachas fuertes. Sin datos nuevos sigue aparcado; `clear` lo libera a mano. Con anemómetro en la caja (`wind.anemoPin`) contesta `ok` y no lo usa |
| `reboot` | — | `ok` y reinicio |

Orden desconocida: `{"e":"err","c":"…","m":"unknown"}`. Línea que no es JSON: `{"e":"err","m":"bad json"}`.

## Eventos

### `info`

```json
{"e":"info","api":1,"lim":{"minAz":0,"maxAz":450,"minEl":0,"maxEl":180},"hasEl":true,"pin":true,
 "dev":"APRS LoRa+ Rotor","ver":"5.4.2","board":"esp32s3","chip":"ESP32-S3","name":"APRS-Rotor",
 "drv":"direct (relays)","call":"EA7JRS-12",
 "caps":{"wifi":true,"ble":true,"spp":false,"lora":false,"cal":true,"move":true},
 "ip":"192.168.1.50","rotctld":4533,"heap":187000,"up":3600,"utc":1762500000}
```

Con CAT configurado: `"cat":true`, `caps.cat` y `"rigctld":4532` (0 = apagado). Con protección por viento (desde la 5.4.0): `"wind":true` y `caps.wind`.

Red local (ESP32 y Raspberry Pi, `caps.lan`): `"clock":{"src":"GPS","age":120,"ntp":"ntp.roa.es"}` (quién puso la hora: `GPS`, `NTP` o `-`; solo ESP32) y, con algo configurado en `lan`, `"lan":{"kind":3,"wifi":true,"tcp":true,"lines":5210,"sk":18000,"udp":120,"age":0,"wxOk":3,"wxFail":0,"wxAge":400,"lat":36.53,"lon":-6.28}`.

`board` es el entorno de PlatformIO (`esp32dev`, `esp32s3`, `waveshare_c5_lcd35`, `waveshare_s3_amoled241`, `waveshare_s3_lcd7b`, `pico2w`, `pico2`) o `raspberrypi`. En la Raspberry `drv` es `hamlib <modelo>` y se añade `hamlib` (estado del rotctld).

### `pos`

```json
{"e":"pos","az":123.4,"el":45.0,"st":38,"mv":true,"f":"OK","maz":483.4,"taz":125.0,"tel":45.0,"aprs":"EA7JRS-9: P 125 45 -> GO AZ 125 EL 45"}
```

- `az` 0-360 (o el mecánico si *reportMech*), `el` solo si hay elevación.
- `st`: banderas de estado de Hamlib (`rot_status_t`): 1 BUSY, 2 MOVING, 4 MOVING_AZ, 8 MOVING_LEFT, 16 MOVING_RIGHT, 32 MOVING_EL, 64 MOVING_UP, 128 MOVING_DOWN, 256 LIMIT_UP, 512 LIMIT_DOWN, 1024 LIMIT_LEFT, 2048 LIMIT_RIGHT, 4096 OVERLAP_UP, 16384 OVERLAP_LEFT, 65536 OVERLAP_RIGHT.
- `f`: `OK`, `STALL AZ`, `STALL EL`, `SENSOR`, `NO LINK` (pasarela serie o rotctld sin respuesta), `WIND` (aparcado por viento).
- `wind` (con `wind.on` o tras recibir `wx`): `{"on":true,"park":15,"lk":true,"ws":20,"wg":28.4,"wd":250,"pk":28.4,"age":150,"src":"OWM","hold":870}`: `lk` = aparcado, `pk` = el mayor de medio y racha, `age` = segundos de la observación, `hold` = segundos que faltan como mínimo para liberar. Con anemómetro: `"src":"anemo","local":true`, y `raw` lleva `anemoHz` y `anemoPulses` para calibrarlo.
- `hdg` (con `lan.heading` o si llega rumbo por la red): `{"on":true,"v":123.4,"age":1}`; `on` = el acimut del evento es verdadero (compensado con el rumbo), `old` = sin rumbo nuevo hace más de 10 s. Con viento medido por la red, `wind` lleva `"src":"lan"` o `"signalk"` y `"local":true`; con el de OpenWeatherMap de la caja, `"src":"owm"`.
- `maz`: acimut mecánico (control directo; > 360 = en el solape). `taz` / `tel`: objetivo. `aprs`: última orden APRS.

### `cat`

```json
{"e":"cat","rig":"IC-9700 · CI-V A2 · 19200","l":true,"f":436795000,"m":"FM","tf":145850000,"sp":true,"v":"A",
 "ptt":false,"sm":7,"tn":67.0,"pa":false,"tr":{"en":true,"on":true,"n":"SO-50","dop":-4210}}
```

`l` enlace con la emisora, `f` / `tf` Hz (bajada / subida), `sp` split, `v` VFO, `sm` S-meter 0-15 (S0-S9, S9+10…+60; -1 sin dato), `tn` tono CTCSS (si está puesto), `pa` PTT permitido, `tr` seguimiento (`en` activado, `on` con datos recientes, `n` nombre, `dop` Hz de Doppler de bajada). Con `sub`, la caja lo manda cada segundo.

### `raw`

Control directo: `{"e":"raw","az":2105,"el":1460,"unit":"mV"}` (`mV` ESP32, `adc` cuentas de 12 bits RP2350, `ads` ADS1115). Pasarela / Raspberry: `{"e":"raw","link":true}`.

### `cfg`

`{"e":"cfg","i":0,"n":9,"d":"{\"ver\":\"5.4.2\",\"name\":…"}`: se concatenan los `d` en orden. Las contraseñas van incluidas (la orden exige PIN). Para subir: los mismos trozos (≤ 180 caracteres) con `{"c":"cfgset","i":k,"n":N,"d":"…"}` en orden; un trozo fuera de orden responde `order` y hay que empezar de nuevo. Vale una configuración parcial (solo las claves que cambian); `"********"` en un campo secreto lo deja como estaba.
