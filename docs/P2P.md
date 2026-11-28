# Enlace punto a punto (P2P)

Apuntar la antena a **otra estación** calculando el azimut y la elevación desde la posición propia. Si las dos unidades lo hacen, quedan **alineadas mutuamente** (enlaces SHF punto a punto, o dos estaciones móviles que se siguen en vivo).

Es un tercer objetivo de la orden `track` (junto a `sun` y `moon`): `{"c":"track","t":"p2p"}`. La posición **propia** sale del GPS (`gnss.py`), de la red (`lan.pos`) o de la fija (`aprs.lat/lon`), en ese orden. La posición del **otro extremo** la da `p2p.src`. Cualquier `pos`/`stop`/`park`/`move` de un cliente deja de seguir, como con el sol/la luna.

Matemática en `geo.py`: azimut de círculo máximo (rumbo verdadero), distancia (haversine) y elevación con la diferencia de altura y, si `p2p.curv`, la caída por **curvatura terrestre** (`el ≈ atan((Δh − d²/2R)/d)`; en enlaces largos a nivel del mar sale ~0 o ligeramente negativa, y el enlace apunta igual). También convierte **locator Maidenhead** ↔ lat/lon.

## Configuración (`p2p`)

| Clave | Valor | Para |
|---|---|---|
| `src` | `fixed` / `net` / `aprs` | de dónde sale la posición del otro extremo |
| `lat`, `lon`, `alt` | grados, grados, m | posición fija (`src=fixed`) |
| `loc` | locator Maidenhead (4/6/8) | alternativa a `lat/lon` en `fixed` |
| `call` | indicativo | baliza a escuchar (`src=aprs`); con SSID = exacto, sin SSID = cualquiera de ese indicativo |
| `curv` | bool | tener en cuenta la curvatura terrestre en la elevación |

## Las tres fuentes

1. **Fija (`src=fixed`)** — metes el `loc` o las `lat/lon/alt` de la otra estación. Para enlaces punto a punto fijos.
2. **En vivo por red / app (`src=net`)** — la app (u otro cliente) envía la posición con la orden `peer` (`{"c":"peer","lat":…,"lon":…,"alt":…}` o `{"c":"peer","loc":"…"}`). Si está siguiendo `p2p`, re-apunta al momento. Para **dos estaciones móviles** (barco, vehículo) que se siguen.
3. **En vivo por APRS/LoRa (`src=aprs`)** — oye la **baliza de posición** del indicativo `p2p.call` (posición APRS sin comprimir) y re-apunta. Funciona cuando APRS está activado (`aprs.enabled`). No soporta posición comprimida.

## Evento `pos`

Siguiendo P2P, el evento `pos` lleva `trk`: `{"t":"p2p","az":74.3,"el":-0.1,"below":false,"km":42.5}` (az/el hacia el otro extremo y distancia). `caps.p2p` indica que el modo está disponible.

## Notas

- El azimut es verdadero; combina con la compensación de rumbo (`heading.py`) si la estación es móvil.
- La elevación se recorta a los topes del eje (`minEl`/`maxEl`).
- Copyright (C) 2026 Manuel - EA7JRS, GPL-3.0-or-later.
