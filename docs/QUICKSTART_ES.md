# APRS LoRa+ Rotor — Guía rápida

*Versión 5.3.5 · © 2026 Manuel - EA7JRS · [English](QUICKSTART_EN.md) · Manual completo: [MANUAL_ES.md](MANUAL_ES.md)*

Controlador de rotores de antena compatible con Hamlib. Lo manejan el tracker APRS LoRa+, la app Android APRS LoRa+, la propia pantalla táctil (placas Waveshare) y cualquier programa con Hamlib (GPredict, Look4Sat, PstRotator…).

## 1. Elige la placa

| Placa | Para qué |
|---|---|
| ESP32 DevKit (`esp32dev`) | Control directo o pasarela; único con Bluetooth clásico (SPP) para el tracker con ESP32 original |
| ESP32-S3 DevKitC-1 (`esp32s3`) | Control directo o pasarela, BLE para la app |
| Waveshare ESP32-C5-Touch-LCD-3.5 (`waveshare_c5_lcd35`) | Caja con pantalla táctil de 3,5" (WiFi 2,4 y 5 GHz) |
| Waveshare ESP32-S3-Touch-AMOLED-2.41 (`waveshare_s3_amoled241`) | Caja con pantalla AMOLED apaisada |
| Raspberry Pi Pico 2 W / Pico 2 (`pico2w`, `pico2`) | Control directo o pasarela; LoRa con el HAT SX1262 |
| Raspberry Pi (Zero 2 W, 3, 4, 5) | Cualquier rotor de Hamlib por su controlador (ver el manual) |

## 2. Instala el firmware

1. Abre **https://ea7jrs.github.io/APRS-LoRa-Plus/** en Chrome o Edge.
2. Elige **Rotor**, conecta la placa por USB y pulsa **Instalar**.
3. Pico 2: descarga el `.uf2`, mantén pulsado **BOOTSEL** al conectarla y copia el fichero a la unidad que aparece.

## 3. Primera configuración

1. Conéctate a la WiFi **APRS-Rotor** (contraseña `aprsrotor`).
2. Abre **http://192.168.4.1/** (usuario `admin`, contraseña `123456`).
3. En **Configuración** (*Leer*, editar, *Guardar y reiniciar*):
   - `wifi.mode: 1`, `wifi.ssid` y `wifi.pass` de tu red.
   - `pin`: cambia el `123456` por el tuyo.
   - `aprs.call`: tu indicativo con SSID para el rotor (por ejemplo `EA7JRS-12`).
   - `driver`: `0` control directo, `1` pasarela serie a un controlador comercial.
4. Tras reiniciar, la WebUI está en **http://APRS-Rotor.local/** (o en la IP que le dé tu router).

Si la placa no entra en tu WiFi en 30 s, vuelve a abrir el punto de acceso **APRS-Rotor** para corregirlo.

## 4. Conecta el rotor

> ⚠️ Las placas trabajan a **3,3 V**. Nunca conectes las líneas del mando directamente a un pin.

**Control directo (Yaesu G-5500 por el DIN de 8 pines):**

| DIN | Función | A la placa |
|---|---|---|
| 6 | posición AZ (2-4,5 V) | divisor 10 kΩ / 18 kΩ → ADC AZ |
| 1 | posición EL (2-4,5 V) | divisor 10 kΩ / 18 kΩ → ADC EL |
| 2 / 4 | CW / CCW (a masa) | transistor NPN o ULN2003 ← pines AZ CW / CCW |
| 3 / 5 | UP / DOWN (a masa) | transistor NPN o ULN2003 ← pines EL UP / DOWN |
| 8 | masa | GND |

Pines por defecto de cada placa: [HARDWARE.md](HARDWARE.md).

**Pasarela serie:** UART de la placa → MAX3232 → controlador (GS-232A/B, Rot2Prog, MD-01/02, EasyComm II). Pon `serial.proto` y `serial.baud`.

## 5. Calibra (solo control directo)

En la WebUI, **Calibración**: lleva el rotor a un extremo (por ejemplo 0°), escribe los grados y pulsa **Punto A aquí**; ve al otro extremo (450°) y pulsa **Punto B aquí**. Prueba con **Ir** a 90°, 180° y 270°.

## 6. Úsalo

- **WebUI**: posición, **Ir**, CCW / CW / UP / DOWN mientras pulsas, **STOP**, **Park**.
- **App Android** (pestaña *Rotor*): busca la caja por Bluetooth o escribe `APRS-Rotor.local`, pon el PIN y manéjala; en *Follow* la caja sigue al tracker, una estación APRS o una baliza CW.
- **Tracker APRS LoRa+**: WebUI del tracker → *Antenna rotor* → *Transport* = TCP, *Host* = `APRS-Rotor.local`, *Port* = 4533 (o Bluetooth SPP con un ESP32 original).
- **GPredict / Look4Sat / PstRotator**: Hamlib «NET rotctl», `IP:4533`.
- **PC por USB**: la placa se presenta como un GS-232B (`rotctl -m 603 -r /dev/ttyUSB0`).
- **Pantalla táctil**: mantén pulsado un botón para mover; mantén la brújula medio segundo, arrastra y suelta para apuntar.
- **Mensajes APRS por LoRa**: manda `P 123 45` al indicativo del rotor (placas con módulo LoRa).
- **Emisora o SDR (CAT, opcional)**: emisora a `cat.rx` / `cat.tx` (3,3 V; MAX3232 si es RS-232), `cat.proto` (1 Icom, 2 Yaesu, 3 FT-817/857/897, 4 Kenwood/Elecraft, 5 SDRconnect para el RSPdx-R2, 6 SDR++/gqrx), `cat.baud`, `cat.civ` (Icom). Se maneja con el botón **CAT** de la pantalla, la WebUI, la app (*Radio (CAT)*, con Doppler de satélites) o por rigctld en el puerto 4532. PTT apagado salvo `cat.ptt: true`. Ver [CAT.md](CAT.md).

## Si algo falla

| Síntoma | Qué mirar |
|---|---|
| No se mueve y marca fallo | Pulsa **Borrar fallo**; revisa el atasco (`stallMs`), los topes y la lectura del potenciómetro |
| Gira al revés | Intercambia `pinA` / `pinB` del eje o los grados de los puntos A y B |
| La lectura salta | Condensador de 100 nF en la entrada del ADC; sube el filtro (`filter` más bajo) o usa un ADS1115 |
| La app no controla | El PIN de la app debe ser el de la caja (`pin`) |
| GPredict no conecta | Puerto 4533, `net.rotctld` activado, misma red |
