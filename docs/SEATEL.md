<!-- Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later -->

# Antena Sea Tel como rotor del ecosistema APRS LoRa+

Una antena marina **Sea Tel** (con su PCU) se maneja desde el ecosistema APRS LoRa+ a través de `raspberrypi/seatel_rotctld.py`: un servidor **rotctld** (Hamlib) que habla el protocolo serie RS422 de la PCU y la presenta como un rotor normal. Así la mueven la app Android, la WebUI, el tracker, Gpredict, Look4Sat, SatDump, S.A.T.-APRS+ y cualquier programa con Hamlib; Stellarium entra por el puente LX200 opcional. Manual completo: [Manual_seatel_rotctld.pdf](Manual_seatel_rotctld.pdf).

> **Seguridad.** El protocolo de la PCU **no tiene orden de parada** y este programa **no es una parada de emergencia**: ten a mano el corte de alimentación de la antena. Sin `--allow-real-motion` solo consulta la posición (modo ensayo). Empieza siempre en ensayo y con recorridos cortos.

## Conexión

Conversor **USB a RS422** (p. ej. el Waveshare USB TO RS232/485/422/TTL), 9600 8N1, por las bornas RS422. En Linux aparece como `/dev/ttyUSB0`. El puerto solo admite un programa a la vez.

## Con la caja APRS LoRa+ Rotor (Raspberry Pi)

```sh
sudo ./install.sh --seatel                 # instala pyserial y el servicio (puerto 4543, ENSAYO)
SEATEL_DEV=/dev/ttyUSB0 sudo ./install.sh --seatel   # otro puerto
```

El servicio `aprs-lora-plus-seatel` arranca `seatel_rotctld` en el puerto **4543** en modo **ensayo**. Cuando lo hayas probado, añade `--allow-real-motion` a su línea `ExecStart` (`sudo systemctl edit --full aprs-lora-plus-seatel`) y reinícialo. Luego, en la configuración del demonio del rotor (WebUI ⚙ > Rotor, o `config.json`):

```json
"driver": "hamlib",
"hamlib": { "model": 2, "device": "127.0.0.1:4543" }
```

El modelo **2** de Hamlib es *NET rotctl*: el demonio se conecta al `seatel_rotctld` y, a partir de ahí, la Sea Tel es una antena más del ecosistema (app, WebUI, seguimiento de Sol / Luna / satélites / H1, etc.).

## Suelto (Gpredict, Stellarium, rotctl)

```sh
python3 seatel_rotctld.py --pcu /dev/ttyUSB0 --port 4533 --allow-real-motion --log seatel.log
python3 seatel_rotctld.py --pcu /dev/ttyUSB0 --lx200 10001 --lat 38.5878 --lon -0.6714   # Stellarium
python3 seatel_rotctld.py --pcu sim                       # PCU simulada, sin antena (pruebas)
```

- **Gpredict / Look4Sat**: rotor modelo **2 (NET rotctl)**, host de la Pi, puerto 4533; acimut 0–360, elevación según `--min-el/--max-el`.
- **Offset de acimut**: si la antena apunta a algo de acimut conocido (el Sol, la Luna o un satélite geoestacionario) y hay diferencia, corrígela con `--az-offset` (acimut verdadero − acimut de la PCU).
- Opciones completas (`--min-interval`, `--wait-arrival`, `--allow-north-crossing`, `--stop-mode`, `--udp-port`…) en el manual PDF.

## Estado

Probado con una **PCU simulada** (18 casos en `tests/test_seatel_rotctld.py`, parte de la CI). **Sin probar con la antena real** ni con Gpredict/Stellarium: sigue la puesta en marcha del manual antes de un pase real.
