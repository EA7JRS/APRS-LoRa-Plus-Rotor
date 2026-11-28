# Changelog — APRS LoRa+ Rotor

Mismo número de versión en todos los repositorios del proyecto (tracker, rotor, app, gateway).

## 5.5.1

- **Raspberry Pi — tercer eje (inclinación / cross-level)** con un driver aparte (**TMC2209**, STEP 5 / DIR 6 / ENABLE 23, activo bajo), para antenas estabilizadas como el domo **Satlink FB250+**. Independiente de az/el (rotctld sigue manejando solo az/el), con `enActive` por eje y topes `minX`/`maxX` (±41,25°). Se manda con la orden `axis` (API / WebUI / app); en móvil, para nivelar el balanceo.
- **Selector de antenas**: un archivo JSON por antena con sus datos mecánicos (ejes, `gear`, micropasos, topes, `enActive`) que se superpone sobre `stepper`. La misma caja mueve distintas antenas eligiendo su perfil. De serie `fb250plus` (domo FB250+, 3 ejes) y `generic-2axis`; los del usuario en `/etc/aprs-lora-plus-rotor/antennas/`. Órdenes `antennas` (lista) y `antenna` (elegir). Ver `docs/RASPBERRY_HATS.md`.
- **Enlace punto a punto (P2P)**: nuevo objetivo `track p2p` que apunta a otra estación desde la posición propia (geodesia en `geo.py`: rumbo verdadero, distancia, elevación con curvatura, y locator Maidenhead). La posición del otro extremo es fija (locator o coordenadas), en vivo por red (orden `peer`, la app) o por APRS (baliza del indicativo `p2p.call`). Alineación mutua para enlaces SHF o estaciones móviles. Ver `docs/P2P.md`.
- **WebUI**: aspecto **claro / oscuro / automático** (el del sistema), con botón en la cabecera y la elección guardada por navegador, como el resto de WebUI de EA7JRS. La esfera se queda oscura.
- **Control remoto desde otras apps** documentado (`docs/CONTROL_REMOTO.md`): Gpredict, SatDump y Look4Sat directos por rotctld (`IP:4533`); SkyRoof con un `rotctld.exe -m 2` local; acceso desde fuera por VPN.
- Corrección: `--print-default` imprime solo el JSON (el banner informativo va a stderr), para que `install.sh` genere un `config.json` válido al instalar en modo stepper.

Copyright (C) 2026 Manuel - EA7JRS · GPL-3.0-or-later
