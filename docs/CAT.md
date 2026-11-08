# CAT: emisoras y receptores SDR

Además del rotor, la caja maneja una emisora o un receptor SDR: frecuencia, modo, VFO A/B, *split*, tono CTCSS, S-meter, PTT y seguimiento con Doppler de satélites (con modo, split y tono del satélite). Se controla desde la pantalla táctil (placas Waveshare), la WebUI, la app Android (pestaña *Rotor > Radio (CAT)*) y cualquier programa con Hamlib por el **rigctld** de la caja (puerto 4532: GPredict, WSJT-X, Look4Sat, SatPC32 con un puente...).

## Protocolos

| `cat.proto` | Equipos | Enlace |
|---|---|---|
| 0 | — (apagado) | |
| 1 Icom CI-V | IC-705, IC-7100, IC-7300, IC-7610, IC-9100, IC-910, IC-9700, IC-821... | UART (`civ` = dirección: A2 IC-9700, 94 IC-7300, A4 IC-705, 98 IC-7610, 7C IC-9100, 60 IC-910) |
| 2 Yaesu (texto) | FT-991/A, FT-710, FTDX10, FTDX101, FT-891, FT-DX3000... | UART |
| 3 Yaesu (5 bytes) | FT-817/818, FT-857, FT-897 | UART (4800 / 9600 / 38400) |
| 4 Kenwood / Elecraft | TS-2000, TS-480, TS-590, TS-890, K3, K4, KX2, KX3; QRP Labs QMX/QDX | UART |
| 5 SDRconnect | SDRplay **RSPdx-R2**, RSPdx, RSP1B, RSP2, RSPduo con SDRconnect (Windows, macOS, Linux, Raspberry Pi 4/5 sin pantalla) | WiFi, WebSocket, puerto 5454 |
| 6 rigctl | SDR++, gqrx, un `rigctld` de Hamlib de la red, otra caja | WiFi, TCP (4532 SDR++, 7356 gqrx) |

En la **Raspberry Pi** la sección `cat` es distinta (ver abajo): `hamlib` arranca el `rigctld` real de Hamlib con cualquiera de sus 300+ emisoras.

### SDRplay RSPdx-R2

El RSPdx-R2 no tiene puerto CAT: lo maneja el programa que lo usa. **SDRconnect** trae una API WebSocket (puerto 5454; activarla en sus ajustes) y la caja la usa: frecuencia del VFO (si se sale de la banda que se ve, mueve también el centro), demodulador (NFM, WFM, AM, USB, LSB, CW), nivel de señal (S-meter aproximado: dBm + 73 = S9). Es receptor: no hay PTT ni frecuencia de subida. SDRconnect puede ir sin pantalla en una Raspberry Pi 4/5 y la misma Pi llevar el demonio del rotor (`cat.type: "sdrconnect"`, `host: "127.0.0.1"`). Con **SDR++** o **gqrx** se usa el protocolo 6 (rigctl). SDRuno no sirve: su CAT es un puerto COM virtual del PC.

## Conexión (UART de las placas)

Pines por defecto (`cat.rx` / `cat.tx`):

| Placa | RX | TX | Notas |
|---|---|---|---|
| ESP32 (`esp32dev`) | 32 | 33 | UART1 |
| ESP32-S3 | 15 | 16 | UART1 |
| Waveshare ESP32-C5-Touch-LCD-3.5 | 12 | 11 | U0RXD / U0TXD: la misma UART que la pasarela serie (el C5 solo tiene dos); CAT **o** pasarela |
| Waveshare ESP32-S3-Touch-AMOLED-2.41 | 8 | 42 | UART1 |
| Pico 2 / Pico 2 W | — | — | se eligen; la UART libre que admita esos pines (UART0: RX GP1, 13, 17, 29; UART1: el resto) |

Niveles: las placas son de **3,3 V**. Nunca directamente a un RS-232 (±12 V).

- **Icom CI-V** (jack de 3,5 mm, una sola línea): interfaz CI-V a TTL (p. ej. diodo 1N4148 de TX a la línea con el cátodo hacia TX, RX directo a la línea, resistencia de 10 k a 3,3 V) o el CT-17. RX y TX en la misma línea: la caja ignora su propio eco. Baudios como *CI-V Baud Rate* de la emisora (19200 por defecto; no «Auto»). *CI-V Transceive* puede quedar en ON.
- **Yaesu / Kenwood con RS-232 (DB-9)**: convertidor MAX3232 (3,3 V). Con USB solamente (FT-991A, IC-7300, IC-9700, TS-590SG...) las placas no sirven de anfitrión USB: usar el puerto de datos / RS-232 si lo tiene, o la Raspberry Pi (USB y Hamlib).
- **FT-817/857/897**: conector ACC / CAT de 8 pines con cable TTL (CT-62 o equivalente a 3,3 V).
- **QMX / QDX / KX2 / KX3**: TTL directo si el equipo es de 3,3 V; si no, divisor en RX.

Masa común entre la caja y la emisora.

## Configuración

Placas (`/rotor_conf.json`, sección `cat`; ver [CONFIG.md](CONFIG.md)):

```json
"cat": {"proto": 1, "baud": 19200, "rx": 15, "tx": 16, "civ": 162, "model": "IC-9700", "pollMs": 120,
        "ptt": false, "pttMaxS": 180, "trackStep": 50, "rigctld": true, "rigctldPort": 4532, "host": "", "port": 0}
```

Raspberry Pi (`/etc/aprs-lora-plus-rotor/config.json`, sección `cat`; ver [RASPBERRY_PI.md](RASPBERRY_PI.md)):

```json
"cat": {"type": "hamlib", "name": "IC-9700", "model": 3081, "device": "/dev/ttyUSB1", "speed": 19200, "civ": "0xA2",
        "ptt": false, "pttMaxS": 180, "trackStep": 50, "server": true, "serverPort": 4532}
```

`type`: `none`, `hamlib` (`rigctl -l` da el número de modelo: 3081 IC-9700, 3073 IC-7300, 3085 IC-705, 1035 FT-991, 2014 TS-2000, 1022 FT-857...; comprobar con `rigctl -l | grep <equipo>`), `rigctl` (`host`, `port`) o `sdrconnect` (`host`, `port` 5454).

## Seguridad del PTT

- Apagado por defecto: hay que poner `cat.ptt: true` para que la pantalla, la WebUI, la app o rigctld puedan transmitir (sin eso responden `ptt not allowed` / `RPRT -9`).
- En la pantalla, la WebUI y la app el PTT solo transmite **mientras se mantiene pulsado**.
- Se suelta solo pasados `pttMaxS` segundos (180 por defecto).
- Los receptores (SDRconnect, rigctl de un SDR) no tienen PTT.

## Doppler de satélites

La caja corrige la frecuencia con la velocidad radial: bajada `f·(1 − v/c)`, subida `f·(1 + v/c)`. La alimenta:

- la **app**: con el tracker en modo SAT, *Rotor > Radio (CAT) > Doppler del satélite* manda con cada evento `sat` del tracker la frecuencia nominal del transmisor elegido (y la subida, si se quiere) y la velocidad radial;
- cualquier cliente con la orden `cattrack` ([CONTROL_PROTOCOL.md](CONTROL_PROTOCOL.md));
- o **GPredict** directamente por el rigctld de la caja (*Radio Interface*: host de la caja, puerto 4532, tipo RX / TX / Duplex).

Sin datos nuevos en 15 s el seguimiento se para. Solo se resintoniza cuando el cambio supera `trackStep` Hz (50 por defecto), como mucho cada 0,5 s.

## Tono CTCSS (subida de satélites FM y repetidores)

`catset` con `tone` (Hz, 0 = apagado), el campo *Tono* de la WebUI, *CTCSS Hz* en la app o `C` + `U TONE 1` por rigctld. Al seguir un satélite con subida, la app pone el tono escrito en ella o, si está vacío, el que trae la descripción del transmisor («FM voice, 67 Hz CTCSS»).

| Emisora | Órdenes |
|---|---|
| Icom CI-V | `1B 00` (tono de repetidor, 3 bytes BCD) y `16 42 01` / `00` |
| Yaesu (texto) | `CN00nnn;` (tabla de 50 tonos) y `CT02;` (solo al emitir) / `CT00;` |
| Kenwood | `TNnn;` (tabla de 42: desde 01 en el TS-2000; desde 00 si `cat.model` contiene 480, 590, 890 o 990) y `TO1;` / `TO0;` |
| FT-817/857/897 | `0B` (tono) y `0A` con `4A` (codificador) / `8A` (apagado) |
| rigctl / Hamlib | `C décimas` y `U TONE 1` / `0` |
| SDRconnect | — (receptor) |

Solo se admiten los tonos estándar de cada tabla. SO-50 necesita además 74,4 Hz durante 2 s para encender el repetidor: ponlo a mano, transmite y vuelve a 67 Hz.

## rigctld de la caja

Servidor Hamlib NET rigctl (protocolo 0, como gqrx) en `rigctldPort` (4532): `f`/`F`, `m`/`M`, `v`/`V`, `t`/`T`, `s`/`S`, `i`/`I`, `x`/`X`, `c`/`C`, `u`/`U TONE`, `l STRENGTH`, `\dump_state`, `\chk_vfo`, `\get_powerstat`, `q`. En Hamlib: `rigctl -m 2 -r IP:4532`; en WSJT-X: *Rig: Hamlib NET rigctl*, *Network Server* `IP:4532`. Lo anuncia por mDNS (`_rigctld._tcp`).

Si la emisora se usa a la vez desde el PC con su propio programa, no conectar ese programa al mismo puerto serie: usar el rigctld de la caja.
