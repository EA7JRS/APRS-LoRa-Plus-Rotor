<!-- Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later -->

# Modelo de apuntado y auto-pico

Los motores paso a paso en lazo abierto, y cualquier montaje, tienen un **desalineado mecánico**: el cero de acimut y elevación no es exacto, los ejes no son perfectamente perpendiculares, el tubo flexa. El **modelo de apuntado** lo corrige a partir de varias **referencias de posición conocida** (como hacen los telescopios), y el **auto-pico** afina la puntería con el SDR.

## Modelo de apuntado

Modelo lineal clásico de un montaje acimut/elevación:

```
Δaz = IA + CA / cos(el) + NP · tan(el)
Δel = IE + TF · cos(el)
```

donde IA, IE son los índices de cada eje; CA la colimación; NP la falta de perpendicularidad az/el; TF la flexión. El ajuste es por mínimos cuadrados: con **1 referencia** corrige solo los índices (IA, IE); con **2**, añade colimación y flexión; con **3 o más**, el modelo completo.

### Calibrar (WebUI: menú ⚙ > Antena > *Modelo de apuntado*)

1. Elige una referencia de posición conocida: **Sol**, **Luna** o **Inmarsat** (geoestacionario, con el GPS). También vale un azimut/elevación escrito a mano.
2. Apunta a ella y **afina a mano** (flechas, o el auto-pico) hasta centrarla (máximo de ruido solar, de señal de la baliza…).
3. Pulsa **Añadir referencia**: guarda la posición verdadera y el ángulo real del rotor.
4. Repite con **varias** referencias repartidas por el cielo (distintos azimut y elevación).
5. Pulsa **Ajustar**: calcula los coeficientes, muestra el error RMS y activa el modelo. A partir de ahí todo el apuntado (Sol, Luna, satélites, H1…) va corregido.

Marca **Aplicar el modelo** para activarlo o desactivarlo sin perder las referencias. Por el protocolo son las órdenes `pointadd`, `pointfit`, `pointclear`, `pointset` y `pointinfo` (ver [CONTROL_PROTOCOL.md](CONTROL_PROTOCOL.md)).

## Auto-pico con el SDR

Si el receptor publica la **potencia de la señal** por la red, el rotor puede buscar el máximo solo: barre una rejilla alrededor del apuntado actual, mide en cada punto y se queda en el centroide ponderado por potencia. Útil para centrar sobre una fuente fuerte (el Sol, la baliza de un satélite) y para recoger referencias precisas del modelo.

Configura la sonda en `peak` ([CONFIG.md](CONFIG.md)): `host`/`port`, `kind` (`line` = una línea con el número, o `rigctl` = `l STRENGTH` a un rigctld), y la rejilla (`span`, `step`, `dwell`). En la WebUI, **Auto-pico (SDR)**; por el protocolo, `peak` con `on` true/false (el progreso va en el campo `peak` del evento `pos`).

## Estado

Modelo y rejilla probados con datos sintéticos (`tests/test_rotor.py`, en la CI). **Sin probar con la antena ni con un receptor real**: el barrido mueve el rotor base directamente, así que empieza con una fuente a la vista y recorridos cortos.
