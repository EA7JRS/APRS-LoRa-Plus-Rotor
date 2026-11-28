# Control remoto del rotor desde otras apps

El rotor es un **servidor rotctld de Hamlib** (NET rotctl) en el puerto **4533**, accesible por la red. Cualquier programa que hable ese protocolo puede moverlo en remoto; solo cambia cómo se conecta cada uno.

Averigua la IP de la Pi en la WebUI (`:8080`) o con `ip a`. En los ejemplos se usa `10.91.33.80`.

## Apps que conectan directas (rotctld por TCP)

No necesitan nada más: apuntas al `IP:4533` de la Pi.

| App | Plataforma | Cómo |
|---|---|---|
| **Gpredict** | Win / Linux / Mac | Preferencias → Interfaces → *Rotators* → Add: Host = IP de la Pi, Port = **4533**, tipo «NET rotctl». En el módulo, *Antenna Control* → el rotor → Engage/Track. |
| **SatDump** | Win / Linux | Tracker → *Rotator* → Controller Daemon TCP/IP: Host = IP de la Pi, Port **4533**. Permite el rotor en otro ordenador del shack. |
| **Look4Sat** | Android | Ajustes → rotator / rotctld: IP de la Pi, puerto **4533**. Es la opción móvil para mover el rotor. |

Comprobación desde cualquier PC con Hamlib instalado:

```
rotctl -m 2 -r 10.91.33.80:4533
```
Escribe `p` (lee posición), `P 180 30` (apunta a az 180 / el 30), `S` (para), `q` (salir).

## SkyRoof (VE3NEA)

SkyRoof no conecta directo: **lanza su propio `rotctld.exe`** de Hamlib en el PC. Para usar la Pi por red, arráncalo en **modo NET rotctl** (modelo 2), que reenvía a la Pi, y deja que SkyRoof hable con ese rotctld local:

```
"C:\Program Files\hamlib-w64-4.5.5\bin\rotctld.exe" -m 2 -r 10.91.33.80:4533
```

Arranca ese `rotctld.exe` **antes** de activar el control de rotor en SkyRoof (Rotator Control). El modelo `-m 2` es «NET rotctl»: SkyRoof → rotctld local → Pi:4533 → motores. (Es el mismo truco que vale para PstRotator: servidor Hamlib rotctld, puerto 4533.)

## ISS Detector

Es sobre todo un **predictor y avisador de pasos**; no controla un rotor genérico por rotctld en red, así que con él no se mueve este rotor. Para móvil usa **Look4Sat**; en PC, **Gpredict** o **SatDump**.

## Acceso desde fuera de la red local (Internet)

⚠️ **El protocolo rotctld no tiene autenticación**: cualquiera que alcance el puerto 4533 puede mover la antena. **No abras el 4533 a Internet** ni lo reenvíes en el router.

Para controlarlo desde fuera, usa una **VPN** a tu red (WireGuard, Tailscale, etc.): una vez dentro de la VPN, las apps ven la Pi como si fuera local (`IP:4533`).

Las vías del rotor que **sí** llevan PIN / TOTP son la **API JSON**, el **BLE** (app Android) y **APRS**; el 4533 (rotctld) es abierto por diseño de Hamlib y se protege con la VPN.

## Notas

- Topes y vuelta por 360°: configura el rango de azimut del rotor en cada app igual que en la Pi (`stepper.minAz`/`maxAz`), para que no dé vueltas de más al cruzar el norte.
- La Pi puede seguir el satélite por su cuenta (orden `track` desde la app APRS LoRa+ o el tracker) o dejar que la app externa le mande az/el por rotctld; no uses las dos a la vez sobre el mismo pase.
- Copyright (C) 2026 Manuel - EA7JRS, GPL-3.0-or-later.
