# APRS LoRa+ Rotor — Quick start guide

*Version 5.3.5 · © 2026 Manuel - EA7JRS · [Español](QUICKSTART_ES.md) · Full manual: [MANUAL_EN.md](MANUAL_EN.md)*

Hamlib-compatible antenna rotor controller. It is driven by the APRS LoRa+ tracker, the APRS LoRa+ Android app, its own touch screen (Waveshare boards) and any Hamlib program (GPredict, Look4Sat, PstRotator…).

## 1. Choose a board

| Board | Use |
|---|---|
| ESP32 DevKit (`esp32dev`) | Direct drive or gateway; the only one with Classic Bluetooth (SPP) for a tracker with the original ESP32 |
| ESP32-S3 DevKitC-1 (`esp32s3`) | Direct drive or gateway, BLE for the app |
| Waveshare ESP32-C5-Touch-LCD-3.5 (`waveshare_c5_lcd35`) | Box with a 3.5" touch screen (2.4 and 5 GHz WiFi) |
| Waveshare ESP32-S3-Touch-AMOLED-2.41 (`waveshare_s3_amoled241`) | Box with a landscape AMOLED screen |
| Waveshare ESP32-S3-Touch-LCD-7B (`waveshare_s3_lcd7b`) | Box with a 7" screen (the APRS LoRa+ Display board); relays through a PCF8574 board on I2C |
| Raspberry Pi Pico 2 W / Pico 2 (`pico2w`, `pico2`) | Direct drive or gateway; LoRa with the SX1262 HAT |
| Raspberry Pi (Zero 2 W, 3, 4, 5) | Any Hamlib rotor through its controller (see the manual) |

## 2. Install the firmware

1. Open **https://ea7jrs.github.io/APRS-LoRa-Plus/** in Chrome or Edge.
2. Choose **Rotor**, plug the board in by USB and press **Install**.
3. Pico 2: download the `.uf2`, hold **BOOTSEL** while plugging it in and copy the file to the drive that appears.

## 3. First setup

1. Join the **APRS-Rotor** WiFi (password `aprsrotor`).
2. Open **http://192.168.4.1/** (user `admin`, password `123456`).
3. In **Configuración** (*Leer* = read, edit, *Guardar y reiniciar* = save and restart):
   - `wifi.mode: 1`, `wifi.ssid` and `wifi.pass` of your network.
   - `pin`: replace `123456` with your own.
   - `aprs.call`: your callsign with an SSID for the rotor (e.g. `EA7JRS-12`).
   - `driver`: `0` direct drive, `1` serial gateway to a commercial controller.
4. After the restart the WebUI is at **http://APRS-Rotor.local/** (or the IP your router gives it).

If the board cannot join your WiFi within 30 s, it opens the **APRS-Rotor** access point again so you can fix it.

## 4. Connect the rotor

> ⚠️ The boards run at **3.3 V**. Never connect the controller lines straight to a pin.

**Direct drive (Yaesu G-5500 through the 8-pin DIN):**

| DIN | Function | To the board |
|---|---|---|
| 6 | AZ position (2-4.5 V) | 10 kΩ / 18 kΩ divider → AZ ADC |
| 1 | EL position (2-4.5 V) | 10 kΩ / 18 kΩ divider → EL ADC |
| 2 / 4 | CW / CCW (to ground) | NPN transistor or ULN2003 ← AZ CW / CCW pins |
| 3 / 5 | UP / DOWN (to ground) | NPN transistor or ULN2003 ← EL UP / DOWN pins |
| 8 | ground | GND |

Default pins for each board: [HARDWARE.md](HARDWARE.md).

**Serial gateway:** board UART → MAX3232 → controller (GS-232A/B, Rot2Prog, MD-01/02, EasyComm II). Set `serial.proto` and `serial.baud`.

## 5. Calibrate (direct drive only)

In the WebUI, **Calibración**: take the rotor to one end (e.g. 0°), type the degrees and press **Punto A aquí** (point A here); go to the other end (450°) and press **Punto B aquí**. Test with **Ir** (go) to 90°, 180° and 270°.

## 6. Use it

- **WebUI**: position, **Ir** (go), CCW / CW / UP / DOWN while pressed, **STOP**, **Park**.
- **Android app** (*Rotor* tab): scan for the box by Bluetooth or type `APRS-Rotor.local`, enter the PIN and drive it; in *Follow* the box follows the tracker, an APRS station or a CW beacon.
- **APRS LoRa+ tracker**: tracker WebUI → *Antenna rotor* → *Transport* = TCP, *Host* = `APRS-Rotor.local`, *Port* = 4533 (or Bluetooth SPP with an original ESP32).
- **GPredict / Look4Sat / PstRotator**: Hamlib "NET rotctl", `IP:4533`.
- **PC over USB**: the board shows up as a GS-232B (`rotctl -m 603 -r /dev/ttyUSB0`).
- **Touch screen**: hold a button to move; hold the compass for half a second, drag and release to aim.
- **Wind (optional)**: with `wind.on: true` and the app's internet weather on, the box parks the rotor on gusts of `wind.park` m/s (15) or more.
- **APRS messages over LoRa**: send `P 123 45` to the rotor's callsign (boards with a LoRa module).
- **Radio or SDR (CAT, optional)**: radio on `cat.rx` / `cat.tx` (3.3 V; MAX3232 for RS-232), `cat.proto` (1 Icom, 2 Yaesu, 3 FT-817/857/897, 4 Kenwood/Elecraft, 5 SDRconnect for the RSPdx-R2, 6 SDR++/gqrx), `cat.baud`, `cat.civ` (Icom). Drive it with the **CAT** button on the screen, the WebUI, the app (*Radio (CAT)*, with satellite Doppler) or over rigctld on port 4532. PTT off unless `cat.ptt: true`. See [CAT.md](CAT.md).

## Troubleshooting

| Symptom | What to check |
|---|---|
| It does not move and shows a fault | Press **Borrar fallo** (clear fault); check the stall timer (`stallMs`), the limits and the potentiometer reading |
| It turns the wrong way | Swap the axis `pinA` / `pinB`, or the degrees of points A and B |
| The reading jumps | 100 nF capacitor on the ADC input; stronger filter (lower `filter`) or an ADS1115 |
| The app cannot control it | The app PIN must match the box (`pin`) |
| GPredict does not connect | Port 4533, `net.rotctld` on, same network |
