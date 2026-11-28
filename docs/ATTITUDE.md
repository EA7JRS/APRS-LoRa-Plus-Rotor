<!--
Copyright (C) 2026 Manuel - EA7JRS
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Compensación de actitud (plataforma móvil y giro del mástil)

Cuando el rotor va en un **coche** o un **barco**, o el **mástil se gira con el viento**, la base no está nivelada
ni fija respecto al norte. Dos capas de software corrigen el apuntado para que la antena siga al objetivo:

- **Rumbo (guiñada / yaw del vehículo)** — ver el apartado de red local y rumbo: resta el rumbo de la plataforma
  al azimut verdadero. El rumbo magnético se pasa a verdadero con la **declinación** (WMM2025 o `lan.magVar`).
- **Actitud (escora, cabeceo y giro del mástil)** — `att.py`, esta página.

**Solo en la Raspberry Pi** (de momento; el firmware ESP32 tiene la cadena pero no la capa de actitud).

## Qué hace
Los objetivos (sol, luna, satélites, P2P) se calculan en **azimut verdadero y elevación sobre el horizonte**. En una
plataforma inclinada, los ejes del rotor están girados respecto al horizonte, así que un az/el nivelado no cae en el
az/el del rotor. La capa `AttitudeRotor` toma la **actitud de la base** —escora (roll), cabeceo (pitch) y giro del
mástil (yaw, de un giróscopo)— y **transforma** el az/el del objetivo al marco inclinado del rotor (una rotación 3D),
de modo que el eje de la antena apunta al objetivo. Vale para **2 y 3 ejes**: en el domo FB250+ (3 ejes) el tercer eje
estabiliza mecánicamente y esto afina lo que no llegue; en una antena de 2 ejes hace toda la compensación (con el límite
natural cerca del cenit).

Va en la cadena por dentro del rumbo: `clientes → SkyRotor → HeadingRotor (rumbo) → AttitudeRotor (actitud) → viento →
rotor`. Reapunta cuando la actitud cambia `att.step` grados o más, como mucho 2 veces por segundo; si el dato se queda
`att.staleS` segundos sin refrescar, se da por viejo (`old`). Con `att.on` apagado o sin dato, el az/el pasa sin tocar.

## Cómo se alimenta
- **Ahora**: orden `att` (API JSON / app): `{"c":"att","r":<escora>,"p":<cabeceo>,"y":<giro mástil>}` en grados.
- **Previsto** (cuando integres el sensor): una **IMU** (acelerómetro + magnetómetro) para escora/cabeceo y un
  **giróscopo** para el giro del mástil por viento; se conectarán a esta misma orden (o por NMEA de la red).

La WebUI muestra en la tarjeta *Posición* la línea «Escora 5° · cabeceo −2° · giro 3°» cuando la actitud está activa.

## Cero (tara)
La compensación **parte de escora y cabeceo 0**: la actitud de referencia no es necesariamente la horizontal del sensor,
sino el montaje real en reposo. Con la antena nivelada y quieta, la orden `{"c":"att","zero":true}` toma la actitud
actual como cero del montaje y la guarda en `att.rollZero`, `att.pitchZero`, `att.yawZero`; a partir de ahí la escora, el
cabeceo y el giro se cuentan **relativos a ese cero**. Así un sensor montado con una pequeña inclinación fija no desvía el
apuntado. Para volver a valores absolutos, pon los tres a 0 en la configuración (o `set_zero(0, 0, 0)`).

## Calibración
Los signos `att.rollSign`, `att.pitchSign`, `att.yawSign` (±1) ajustan el sentido de cada eje según cómo monte la IMU
(qué considera positivo). Convenio por defecto: ejes **X = estribor, Y = proa, Z = arriba**; escora positiva = estribor
abajo, cabeceo positivo = proa arriba, giro positivo = mástil hacia estribor. Un objetivo en el eje de balanceo (proa)
no cambia con la escora; un objetivo a estribor en el horizonte sube en elevación al escorar a estribor. Ajusta los
signos con el sensor puesto hasta que el apuntado siga al objetivo al inclinar la base.

Matemática y pruebas: `aim()` / `unaim()` en `attitude.py` (rotación 3D, ida y vuelta exacta); comprobado en
`tests/test_rotor.py` (`TestAttitude`). **Sin probar con una IMU real.**
