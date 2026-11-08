# Raspberry Pi (Zero 2 W, 3, 4, 5)

```
   rotor ── controlador (GS-232, SPID, EasyComm, K3NG, ARS, ERC…) ── USB / serie ──┐
                                                                                   │
   rotctld de Hamlib (127.0.0.1:4535, lo arranca y vigila el demonio) ◄────────────┘
        ▲
   demonio aprs-lora-plus-rotor (Python, asyncio)
        ├─ rotctld público ............ 4533  (tracker, GPredict, Look4Sat…)
        ├─ protocolo JSON ............. 4534  (app por WiFi)
        ├─ WebUI ...................... 8080
        ├─ BLE (BlueZ) ................ app Android
        ├─ SPP (Bluetooth clásico) .... tracker con GS-232B
        ├─ puertos serie .............. la Pi como controlador (USB gadget de la Zero 2 W)
        └─ APRS por TNC KISS .......... mensajes "P az el" desde LoRa / VHF
```

Vale cualquier rotor que Hamlib conozca: el demonio no habla con el rotor, lo hace el `rotctld` real.

## Instalación

```sh
git clone https://github.com/EA7JRS/APRS-LoRa-Plus-Rotor
cd APRS-LoRa-Plus-Rotor/raspberrypi
sudo ./install.sh              # Zero 2 W como GS-232B por USB para el PC: sudo ./install.sh --gadget
```

Instala `libhamlib-utils`, `python3-dbus-next` y `bluez`, copia el programa a `/opt/aprs-lora-plus-rotor`, crea `/etc/aprs-lora-plus-rotor/config.json` y arranca el servicio `aprs-lora-plus-rotor`.

Elige el modelo de tu controlador (`rotctl -l | less`) y edítalo en la WebUI (`http://IP:8080`, usuario `admin`, contraseña = PIN) o en el fichero:

```json
"hamlib": { "model": 603, "device": "/dev/ttyUSB0", "speed": 9600 }
```

Modelos frecuentes: 601 GS-232A, 603 GS-232B, 202 EasyComm II, 901 SPID Rot2Prog, 903 SPID MD-01/02, 1 Dummy (pruebas sin rotor). Opciones propias del modelo con `"conf": "clave=valor,..."` (se pasan con `-C`). Los límites salen del `\dump_state` del rotctld; se pueden forzar con `minAz`, `maxAz`, `minEl`, `maxEl`, `hasEl`.

Para un nombre de puerto fijo usa `/dev/serial/by-id/...`.

## Configuración

| Clave | Por defecto | |
|---|---|---|
| `name` | `APRS-Rotor` | nombre Bluetooth y de la WebUI |
| `pin` | `123456` | PIN de control (app, WebUI) |
| `hamlib.*` | Dummy | modelo, puerto, velocidad, `conf`, `port` interno (4535), `pollMs` |
| `park.az` / `park.el` | 0 / 0 | posición de reposo |
| `net.*` | 4533 / 4534 / 8080 | rotctld público, JSON, WebUI (`web`, `webUser`, `webPass`; vacía = PIN) |
| `bt.ble` / `bt.spp` | sí / no | BLE para la app; SPP para el tracker (`sppProto` 1 = GS-232B) |
| `serialPorts` | `[]` | `[{"device":"/dev/ttyGS0","baud":9600,"proto":1}]`: la Pi se hace pasar por un controlador (0 GS-232A, 1 GS-232B, 2 EasyComm II, 3 Rot2Prog, 4 rotctld) |
| `cat.*` | apagado | emisora o receptor: `type` `hamlib` (rigctld real de Hamlib en 127.0.0.1:`internalPort` 4536; `model` de `rigctl -l`, `device`, `speed`, `civ`, `conf`), `rigctl` (`host`, `port`: SDR++, gqrx) o `sdrconnect` (`host`, `port` 5454: SDRplay RSPdx-R2); `ptt`, `pttMaxS`, `trackStep`, servidor rigctld público `server` / `serverPort` (4532). Ver [CAT.md](CAT.md) |
| `aprs.*` | apagado | `kiss` = `tcp:IP:8001` (tracker / iGate APRS LoRa+ en KISS, Direwolf) o `serial:/dev/ttyACM0:115200`; `call`, `allow`, `totp`, `reply`, `beaconMin`, `lat`, `lon`, `comment` |

Al guardar desde la WebUI o la app el demonio se reinicia (systemd lo levanta con la configuración nueva).

## Bluetooth

- **BLE**: el demonio registra en BlueZ el servicio GATT del rotor y un anuncio con su nombre. El emparejamiento es «Just Works» (agente sin E/S); el control lo protege el PIN de la orden `auth`.
- **SPP**: perfil Serial Port en el canal 1; el adaptador queda visible con el nombre del rotor para que el tracker lo encuentre.

Comprueba con `bluetoothctl show` que el adaptador está encendido. En la Zero 2 W BLE y WiFi comparten antena: si el enlace va justo, acerca el móvil.

## Registro

```sh
journalctl -u aprs-lora-plus-rotor -f
```

## Probar sin Raspberry

```sh
cd raspberrypi
python3 -m unittest discover -s tests -v
python3 -m aprs_lora_plus_rotor.main -c /tmp/rotor.json -v      # con "model": 1 (Dummy) y rotctld instalado
```
