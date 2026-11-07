# Control del rotor por APRS (LoRa)

El rotor tiene indicativo propio (p. ej. `EA7JRS-12`) y obedece mensajes APRS dirigidos a él. Así el tracker APRS LoRa+ (o cualquier equipo APRS) lo mueve a distancia sin WiFi ni Bluetooth.

- **ESP32 / Pico con módulo LoRa**: escucha directamente en LoRa APRS (433,775 MHz, SF12, BW 125 kHz, CR 4/5, palabra de sincronía 0x12; todo configurable).
- **Raspberry Pi**: por un TNC KISS: el tracker o el iGate APRS LoRa+ en modo KISS por WiFi (`tcp:IP:8001`), Direwolf, o un TNC por puerto serie (`serial:/dev/ttyACM0:115200`).

## Órdenes

Texto del mensaje, sin distinguir mayúsculas:

| Mensaje | Hace |
|---|---|
| `P 123 45` · `P123 45` · `AZ 123 EL 45` | ir a acimut 123°, elevación 45° |
| `P 123` · `AZ 123` | solo acimut |
| `PARK` | posición de reposo |
| `STOP` | parar |
| `?` | posición actual |
| `HELP` | lista de órdenes |

Si el mensaje lleva número (`{17`), el rotor manda el `ack17`. Después contesta (si *reply* está activo) con un mensaje: `GO AZ 123 EL 45`, `AZ 123 EL 45 OK`, `AZ 123 EL 45 MOVING`, o un error (`ERR range`, `ERR cmd (HELP)`, `ERR code`, `ERR no time`). Un mensaje repetido (mismo remitente y número en 3 minutos) se vuelve a confirmar pero no se ejecuta otra vez.

Las respuestas salen 1,5 s después de recibir (el otro equipo vuelve a recepción) y la emisión no bloquea el control del motor (con SF12 una trama dura varios segundos).

## Quién puede mandar

1. **Lista de indicativos** (`aprs.allow`, separados por comas): `EA7JRS` vale para cualquier SSID; `EA7JRS-9` solo para ese. Vacía = el indicativo base del rotor. Un remitente que no está recibe `rej`.
2. **Código TOTP** (opcional, `aprs.totp`): secreto en base32 (el de cualquier app de autenticación: Google Authenticator, Aegis, FreeOTP…; RFC 6238, 6 cifras, 30 s). Se añade al final del mensaje: `P 123 45 492039`. Vale ±30 s y **no se puede reutilizar** (aunque alguien lo copie de la radio, ya no sirve). Necesita la hora: por NTP (WiFi) en las placas, del sistema en la Raspberry. Sin hora, el rotor responde `ERR no time`.

El código autentica pero no cifra: el contenido sigue a la vista, como piden las normas de radioaficionado.

## Baliza de estado

Con `aprs.beaconMin` > 0 el rotor manda cada N minutos su estado: con posición (`lat`/`lon`, símbolo `/r` antena) o como trama de estado `>AZ 123 EL 45 OK APRS LoRa+ Rotor`.

## Seguimiento de satélites por LoRa

LoRa APRS es lento: a SF12 cabe una orden cada pocos segundos y comparte canal con todo el mundo. Para seguir un satélite usa WiFi (rotctld) o BLE; por LoRa manda solo cambios grandes (el tracker puede usar un paso mínimo de 5-10° y un intervalo de 15-30 s). Mejor aún: una frecuencia o un perfil LoRa propio para el enlace con el rotor.

## Ejemplo de tramas

```
EA7JRS-9>APLRT1::EA7JRS-12:P 123 45{17
EA7JRS-12>APZLRR::EA7JRS-9 :ack17
EA7JRS-12>APZLRR::EA7JRS-9 :GO AZ 123 EL 45
```

El destino `APZLRR` es del rango experimental; habrá que registrar uno propio (lista aprs-deviceid) al publicar.
