<!-- Copyright (C) 2026 Manuel - EA7JRS. GPL-3.0-or-later -->

# Radioastronomía: seguir el cielo (línea H1, 1420 MHz)

El rotor puede **seguir un punto fijo del cielo** con el giro sidéreo de la Tierra, pensado para recepción de la **línea de hidrógeno neutro** (H1, 1420,405 MHz) con la antena del domo Satlink FB250+ (banda L). Es **solo apuntado**: la recepción y el procesado van en el receptor SDR aparte.

## Cómo apunta

Las fuentes del cielo se dan en coordenadas **ecuatoriales** (ascensión recta RA y declinación Dec, J2000) o **galácticas** (longitud *l* y latitud *b*). Con la posición del GPS y la hora (GPS o NTP), el rotor calcula el azimut y la elevación en cada instante y reapunta según se mueve el cielo, como con el sol o la luna. Si la fuente está bajo el horizonte, espera a que suba.

- **WebUI**: en la columna de control, botón **📡 H1**. Elige una fuente del desplegable (centro galáctico, plano de Cygnus, anticentro, Casiopea A, Cygnus A), o escribe RA/Dec o *l*/*b*, y pulsa **Seguir (sidéreo)**.
- **Protocolo / app**: orden `track` con `t: "radec"` y `ra`+`dec` o `l`+`b` (ver [CONTROL_PROTOCOL.md](CONTROL_PROTOCOL.md)).

## Barrido por deriva (drift scan)

Para un barrido por deriva **suelto**, apunta a la zona y para el seguimiento (**Dejar**): la antena se queda fija y el cielo cruza el haz solo con la rotación terrestre, a declinación constante.

Para un **barrido automático** por varias declinaciones, pulsa **Barrido por deriva** (botón del panel 📡 H1, o la orden `drift`): la caja apunta al **meridiano** a cada declinación del rango `drift` (de `decStart` a `decEnd` con paso `decStep`), se queda fija `drift.dwell` segundos mientras el cielo cruza el haz, y salta a la siguiente. Registra el apuntado (hora UTC, declinación, az, el) en `drift.csv`, para casarlo luego con lo que grabe el receptor. Necesita posición (GPS) para calcular las elevaciones; las declinaciones que no culminan sobre el horizonte se saltan.

## Nivelación en móvil

En una plataforma móvil, activa la compensación de actitud y la nivelación con el tercer eje (`att.on` + `att.level`): la antena sigue apuntada aunque la base se mueva (ver [ATTITUDE.md](ATTITUDE.md)).

## Notas

- Las coordenadas galácticas se convierten a ecuatoriales J2000 con la matriz de rotación IAU (Hipparcos); el centro galáctico (*l*=0, *b*=0) cae en RA 266,4°, Dec −28,9°.
- El apuntado no corrige el movimiento propio ni la precesión más allá de J2000: sobra para un haz de antena de banda L.
