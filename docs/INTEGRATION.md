# Uso con el tracker APRS LoRa+, la app Android y los programas de satélites

El rotor ofrece lo mismo en todas las placas (según el hardware que tengan):

| Quién lo manda | Por dónde | Qué configurar |
|---|---|---|
| **Tracker APRS LoRa+** (modo SAT, GO TO, objetivos fijos) | WiFi: rotctld | Tracker: *Rotor > Transport* = TCP, *Host* = IP o `APRS-Rotor.local`, *Port* = 4533 |
| | Bluetooth clásico SPP (tracker con ESP32 original) | Rotor: `bt.mode: 2`, `bt.sppProto: 1` (GS-232B). Tracker: *Transport* = Bluetooth SPP, *BT name* = nombre del rotor (`APRS-Rotor`) |
| | LoRa APRS (mensajes) | Rotor con LoRa y `aprs.call`; el tracker manda `P az el` al indicativo del rotor (docs/APRS_CONTROL.md) |
| **App Android APRS LoRa+** | BLE (protocolo JSON) | Rotor: `bt.mode: 1`. Servicio `4c6f5261-2b00-4ea7-b000-000000000001`, PIN de control |
| | WiFi: JSON (4534) o rotctld (4533) | La app ya tiene cliente rotctld (`RotctlLink`): reenvía el evento `rot` del tracker |
| **GPredict, Look4Sat, SatPC32 (con su puente), PstRotator, CSN S.A.T.…** | rotctld por WiFi | Hamlib «NET rotctl» (modelo 2), `IP:4533` |
| **Cualquier programa con Hamlib en el PC** | USB | La placa se hace pasar por un GS-232B (`rotctl -m 603 -r /dev/ttyUSB0`), EasyComm II (202) o Rot2Prog (901) |
| **Navegador** | WiFi | `http://APRS-Rotor.local/` (placas) o `http://IP:8080/` (Raspberry) |

En la Raspberry Pi el rotor real lo maneja el rotctld de Hamlib, así que vale **cualquier modelo de rotor de Hamlib** (`rotctl -l`).

## rotctld

Servidor del protocolo de red de Hamlib 4.x (el de `rotctld`): `P`, `p`, `S`, `K`, `M`, `s` (get_status), `_`, `\dump_state` (protocolo 1 con `min_az=…` y `done`), `\dump_caps`, `q`. Varios clientes a la vez (4 en las placas). Si el cliente pide un acimut por encima de 360 dentro del rango mecánico (p. ej. 400 en un rotor de 450°), se respeta; si pide 0-360, el control elige el giro más corto que no pase por los topes.

> Aviso para el firmware del tracker: el `\dump_state` de `include/rot_proto.h` del tracker responde con el formato antiguo sin la línea `done`. El cliente `netrotctl` de Hamlib 4.x espera las etiquetas `clave=valor` y `done` (ver `rigs/dummy/netrotctl.c`), así que GPredict con Hamlib reciente puede quedarse esperando. El rotor usa el formato correcto (`firmware/lib/RotorCore/src/rc_rotctld.h`); conviene copiar ese bloque al tracker.

## Pendiente en los otros repositorios

- **App Android**: pestaña / sección *Rotor* con el servicio BLE de arriba (conectar, `auth`, `info`, `sub`, posición en una brújula, ir a, mover, parar, park, calibrar, configuración con `cfgget` / `cfgset`) y la opción de mandar el evento `rot` del tracker al rotor por BLE o por el puerto JSON (4534).
- **Tracker**: transporte «LoRa APRS» para el rotor (mandar `P az el` al indicativo del rotor con paso e intervalo mínimos) y el `\dump_state` corregido.
