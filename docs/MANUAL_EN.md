# APRS LoRa+ Rotor — User manual

*Version 5.3.5 · © 2026 Manuel - EA7JRS · GPL-3.0-or-later · [Español](MANUAL_ES.md) · [Quick start](QUICKSTART_EN.md)*

The WebUI and the touch screen are in Spanish; their labels are given here as they appear, with the English meaning in brackets.

## Contents

1. [What it is](#1-what-it-is)
2. [Boards](#2-boards)
3. [Installing the firmware](#3-installing-the-firmware)
4. [First start](#4-first-start)
5. [Connecting the rotor](#5-connecting-the-rotor)
6. [Calibration and control tuning](#6-calibration-and-control-tuning)
7. [WebUI](#7-webui)
8. [Touch screen](#8-touch-screen)
9. [Android app](#9-android-app)
10. [APRS LoRa+ tracker](#10-aprs-lora-tracker)
11. [Hamlib programs and PC](#11-hamlib-programs-and-pc)
12. [Radio and SDR receiver (CAT)](#12-radio-and-sdr-receiver-cat)
13. [Control by APRS messages (LoRa)](#13-control-by-aprs-messages-lora)
14. [Raspberry Pi](#14-raspberry-pi)
15. [Safety and security](#15-safety-and-security)
16. [Updates and LED](#16-updates-and-led)
17. [Troubleshooting](#17-troubleshooting)
18. [Reference](#18-reference)

---

## 1. What it is

**APRS LoRa+ Rotor** is a **Hamlib-compatible** antenna rotor controller. It belongs to the APRS LoRa+ ecosystem together with the tracker and the Android app, and it can be driven from:

- the **APRS LoRa+ tracker**: it follows satellites, APRS stations (moving ones too) and CW beacons;
- the **APRS LoRa+ Android app**, over Bluetooth (BLE) or WiFi;
- its **WebUI** in any browser;
- its own **touch screen**, on the Waveshare boards;
- **any Hamlib program**: GPredict, Look4Sat, PstRotator, CSN S.A.T…;
- a **PC over USB**, where it presents itself as a GS-232B, EasyComm II or Rot2Prog controller;
- **APRS messages over LoRa**, from any authorised station.

It can move the rotor in two ways:

- **Direct drive**: the board replaces the controller's computer interface. It switches the rotation relays or transistors and reads the position from the rotor potentiometer.
- **Serial gateway**: the board talks to a commercial controller (Yaesu GS-232A/B, SPID Rot2Prog / MD-01 / MD-02, EasyComm II: K3NG, SatNOGS, ERC…) or encoder controllers on an addressed RS485 bus.

On the **Raspberry Pi** the rotor is run by Hamlib's `rotctld`, so **any rotor Hamlib knows** will work.

## 2. Boards

| | ESP32 | ESP32-S3 | ESP32-C5 LCD 3.5" | ESP32-S3 AMOLED 2.41" | ESP32-S3 LCD 7" | Pico 2 W | Pico 2 | Raspberry Pi |
|---|---|---|---|---| --- |---|---|---|
| Direct drive | ✔ | ✔ | relays + 1 ADC (or ADS1115) | ✔ | PCF8574 relays (I2C) + 1 ADC (or ADS1115) | ✔ | ✔ | — |
| Serial gateway | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | any Hamlib model |
| WiFi (rotctld, JSON, WebUI) | ✔ | ✔ | ✔ (2.4 and 5 GHz) | ✔ | ✔ | ✔ | — | ✔ |
| BLE for the app | ✔ | ✔ | ✔ | ✔ | ✔ | pending | — | ✔ |
| Bluetooth SPP (GS-232) for the tracker | ✔ | — | — | — | — | ✔ | — | ✔ |
| LoRa APRS | external module | external module | — | — | — | SX1262 HAT | SX1262 HAT | KISS TNC |
| Touch screen | — | — | ✔ portrait | ✔ landscape | ✔ landscape 7" | — | — | — |
| USB as a controller | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | USB gadget (Zero 2 W) |
| Update from the WebUI | 8 MB boards | ✔ | ✔ | ✔ | ✔ | ✔ | — | `git pull` + `install.sh` |

PlatformIO environments: `esp32dev`, `esp32s3`, `waveshare_c5_lcd35`, `waveshare_s3_amoled241`, `waveshare_s3_lcd7b`, `pico2w`, `pico2`.

## 3. Installing the firmware

### Web installer (recommended)

1. Open **https://ea7jrs.github.io/APRS-LoRa-Plus/** in Chrome or Edge (they have Web Serial).
2. Choose **Rotor** and your board, plug it in by USB and press **Install**. The installer recognises the connected board and the version it runs.
3. **Pico 2 / Pico 2 W**: download the `.uf2` file, plug the board in with **BOOTSEL** held down and copy the file to the drive that appears.

### PlatformIO

```sh
cd firmware
pio run -e esp32s3 -t upload        # or esp32dev, waveshare_c5_lcd35, waveshare_s3_amoled241, waveshare_s3_lcd7b, pico2w, pico2
```

### Raspberry Pi

See [section 14](#14-raspberry-pi).

## 4. First start

1. On first boot the board opens the **APRS-Rotor** WiFi network (password `aprsrotor`).
2. Join it and open **http://192.168.4.1/**. User `admin`, password = the PIN (`123456`).
3. In **Configuración** (configuration) press *Leer* (read), change what you need and press *Guardar y reiniciar* (save and restart):

| Key | What to set |
|---|---|
| `name` | box name: WiFi, Bluetooth and `name.local` |
| `pin` | **change it**: it protects the app, the WebUI and control |
| `wifi.mode` | `1` to join your network (`wifi.ssid`, `wifi.pass`); `2` keeps the access point; `0` turns WiFi off |
| `aprs.call` | rotor callsign with an SSID (e.g. `EA7JRS-12`) |
| `driver` | `0` direct drive, `1` serial gateway |
| `bt.mode` | `1` BLE (app), `2` SPP (tracker with the original ESP32), `0` off |

4. After the restart the WebUI is at **http://APRS-Rotor.local/** (or the IP your router gives it).

If the board cannot join your WiFi within 30 s, it opens the **APRS-Rotor** rescue access point so you can fix it.

Every option is listed in [CONFIG.md](CONFIG.md). The configuration is stored on the board (`/rotor_conf.json`), and the app and the WebUI read and write all of it.

## 5. Connecting the rotor

> ⚠️ The boards run at **3.3 V**. No input may receive more: use voltage dividers, and transistors or optocouplers for the outputs. Controller and motor lines never go straight to a pin.

### 5.1 Default pins

| Board | AZ CW | AZ CCW | EL UP | EL DOWN | AZ ADC | EL ADC | I2C (ADS1115) | Gateway UART RX/TX |
|---|---|---|---|---|---|---|---|---|
| ESP32 DevKit | 25 | 26 | 27 | 14 | 34 | 35 | 21 / 22 | 16 / 17 |
| ESP32-S3 DevKitC-1 | 4 | 5 | 6 | 7 | 1 | 2 | 8 / 9 | 18 / 17 |
| ESP32-C5-Touch-LCD-3.5 | EXIO8 | EXIO9 | EXIO10 | EXIO11 | EXIO_ADC | ADS1115 | 27 / 26 | 12 / 11 |
| ESP32-S3-Touch-AMOLED-2.41 | 38 | 39 | 40 | 41 | 1 | 7 | 47 / 48 | 44 / 43 |
| ESP32-S3-Touch-LCD-7B | PCF8574 P0 (120) | P1 (121) | P2 (122) | P3 (123) | GPIO6 (Sensor) | ADS1115 | 8 / 9 | 44 / 43 |
| Pico 2 W / Pico 2 | GP6 | GP7 | GP8 | GP9 | GP26 | GP27 | GP4 / GP5 | GP1 / GP0 |

All of them can be changed in the configuration; `-1` means unused. On the ESP32, use only ADC1 pins, because ADC2 does not work while WiFi is on. The boards with a screen have their own sheets: [WAVESHARE_C5_LCD35.md](WAVESHARE_C5_LCD35.md), [WAVESHARE_S3_AMOLED241.md](WAVESHARE_S3_AMOLED241.md) and [WAVESHARE_S3_LCD7B.md](WAVESHARE_S3_LCD7B.md) (in Spanish). On the 7" board the relays go through a PCF8574 board on I2C (pins 120-127, active on 0: `direct.activeHigh: false`).

### 5.2 Direct drive: Yaesu G-5500 through the 8-pin DIN

"EXTERNAL CONTROL" socket on the back of the controller:

| DIN pin | Function | To the board |
|---|---|---|
| 6 | 2-4.5 V = azimuth 0-450° | divider → AZ ADC |
| 1 | 2-4.5 V = elevation 0-180° | divider → EL ADC |
| 2 | to ground = CW | AZ CW transistor |
| 4 | to ground = CCW | AZ CCW transistor |
| 3 | to ground = UP | EL UP transistor |
| 5 | to ground = DOWN | EL DOWN transistor |
| 8 | common ground | GND |

- **Position**: on each output, a divider of 10 kΩ (top) and 18 kΩ (bottom), with 100 nF to ground at the ADC input. The defaults already account for this divider.
- **Rotation**: one NPN (BC547 / 2N2222, 2.2 kΩ to the base and 10 kΩ from base to ground) or one ULN2003 channel per line. The collector goes to the DIN pin and the emitter to ground. With optocouplers (PC817) the two grounds stay isolated.
- The G-5500 needs a pause before reversing (`reverseMs` 1200).

### 5.3 Other rotors in direct drive

- **Single axis** (G-450, G-650, G-1000DXC…): `direct.elEnabled: false`.
- **Variable speed** (DXC series, G-2800): a pin in `pinPwm`, with an RC filter into the controller's speed input.
- **Hy-Gain with brake** (Ham-IV, T2X): brake relay on `brakePin` and `brakeMs` of 2000 to 3000.
- **DC motor with an H-bridge** (BTS7960, L298N, DRV8871): `outMode: 1`, with soft start (`rampMs`) and gradual braking (`slowZone`, `minSpeed`).
- **ADS1115** (16-bit, I2C): `sensor: 1` and the channel in `adsCh`.
- **No sensor** (`sensor: 2`): manual movement only, which stops by itself after 90 s.

### 5.4 Serial gateway

Board UART → controller. With RS-232, use a MAX3232 at 3.3 V; with 5 V TTL, a divider on RX.

| Controller | `serial.proto` | Speed |
|---|---|---|
| Yaesu GS-232A | 0 | 9600 |
| Yaesu GS-232B, K3NG in Yaesu mode | 1 | 9600 |
| EasyComm II (K3NG, SatNOGS, ERC) | 2 | 9600 / 19200 |
| SPID Rot2Prog, MD-01, MD-02 | 3 | 600 / 460800 |
| Addressed RS485 bus (one controller per axis; `serial.busId`, `azId`, `elId`) | 4 | 115200 |

`serial.hasEl: false` for azimuth-only rotors. `minAz`, `maxAz` and `maxEl` are the limits announced to Hamlib.

More in [HARDWARE.md](HARDWARE.md) (in Spanish).

## 6. Calibration and control tuning

### Two-point calibration

From the WebUI (*Calibración*) or the app (*Rotor* → calibration):

1. Drive the rotor with the buttons to a known end (e.g. 0°), type the degrees and press **Punto A aquí** (point A here).
2. Drive it to the other end (e.g. 450° or 180°) and press **Punto B aquí** (point B here).
3. **Corregir desfase** (fix offset): if it is only a few degrees out, type where it really points and press it.

The raw reading (millivolts or counts) is shown next to the position.

### Axis settings (`direct.az`, `direct.el`)

| Setting | Default | Purpose |
|---|---|---|
| `min` / `max` | 0 / 450 (AZ), 0 / 180 (EL) | mechanical range; with the stop to the south, -180 / 180 |
| `deadband` | 1.0° | stops when the error is smaller |
| `restart` | 2.0° | moves again if the error exceeds this (no hunting) |
| `slowZone`, `minSpeed`, `maxSpeed` | 10°, 0.35, 1 | slowing down near the target (PWM) |
| `rampMs` | 600 | soft start |
| `reverseMs` | 1200 | pause before reversing |
| `stallMs` / `stallDeg` | 5000 / 1.0 | stall: if it does not advance, it stops and reports a fault |
| `limitMargin` | 0.5° | stops before the end stop |
| `filter` | 0.35 | reading filter (1 = no filter) |

**Overlap (450° rotors)**: when asked for a heading of 0 to 360°, the controller chooses the shortest turn that does not cross the end stops. With `reportMech: true`, the rotor reports the mechanical azimuth (above 360 in the overlap zone).

## 7. WebUI

At `http://APRS-Rotor.local/` (boards) or `http://IP:8080/` (Raspberry Pi). User `admin`, password = PIN (or `net.webPass`).

| Section | What it does |
|---|---|
| **Posición** (position) | current azimuth and elevation, target, status and raw reading |
| **Mando** (control) | **Ir** (go) to an azimuth and elevation; CCW / CW / UP / DOWN while held; **STOP**; **Park**; **Borrar fallo** (clear fault) |
| **Calibración** (calibration) | point A, point B and offset fix (direct drive) |
| **CAT** | (with `cat.proto`) frequency, step, modes, A/B, CTCSS tone, S-meter, PTT and Doppler tracking |
| **Configuración** (configuration) | the whole file: *Leer* (read), edit, *Guardar y reiniciar* (save and restart); passwords show as `********` and are left unchanged |
| **Actualizar firmware** (update firmware) | upload a `firmware.bin` |
| **Acerca de** (about) | version, board and © 2026 Manuel - EA7JRS |

## 8. Touch screen

**Waveshare ESP32-C5-Touch-LCD-3.5** (portrait), **ESP32-S3-Touch-AMOLED-2.41** and **ESP32-S3-Touch-LCD-7B** (landscape) boards.

- **Compass**: red needle = position, blue = target, orange arc = overlap zone.
- **Buttons**: **CCW · STOP · CW · DOWN · PARK · UP**. The movement buttons move while held.
- **Aiming**: hold the compass for half a second, drag to the heading (green needle) and release. A short tap does not move anything.
- **Fault**: shown in red; tap it to clear it.
- **CAT** (header button, with `cat.proto`): frequency, − / step / +, LSB · USB · CW · FM · AM · DIG, *Seguir* (Doppler), A / B and PTT while held. The CTCSS tone shows next to the radio's name.
- **ⓘ**: name, callsign, board, WiFi, IP, ports, Bluetooth, uptime, **Acerca de** (about) and brightness.
- **Brightness and sleep**: `lcd.bright` and `lcd.dimS`. The LCD dims; the AMOLED turns off so it does not get burn-in. The first touch only wakes it.

## 9. Android app

**Rotor** tab of the APRS LoRa+ app:

1. **Connect**: *Scan for rotor boxes* (BLE) or *Address* = `APRS-Rotor.local` / IP (WiFi, port 4534).
2. **PIN**: the box PIN (`pin`). Without the right PIN you can only read.
3. **Control**: compass, go to, move while held, STOP, Park, *Clear fault*.
4. **Follow**: the box follows
   - the tracker's aim (satellite, GO TO, fixed target…);
   - an APRS station from the tracker's list, a given one or the latest that passes the filter, with estimation when it moves;
   - a CW beacon with a locator (`VVV DE ED7YAA IM76HR`), from the tracker's or the phone's CW decoder.

   Angles are computed from the tracker's GNSS position.
5. **Radio (CAT)**: frequency, step, modes, A/B, split, CTCSS tone, S-meter, PTT and the Doppler of the tracker's satellite ([section 12](#12-radio-and-sdr-receiver-cat)).
6. **Calibration** (Point A / Point B / Offset), full **configuration** (*Read*, *Save and restart*) and *Restart the rotor box*.
7. **Wind**: with the app's internet weather on (*Sensors > Internet weather*, OpenWeatherMap or IQAir) the app sends the wind to the box; with `wind.on: true` the box parks and locks the rotor on gusts of `wind.park` m/s or more (see [section 15](#15-safety-and-security)).

Do not let the tracker drive the same rotor at the same time over WiFi or SPP: pick only one path.

## 10. APRS LoRa+ tracker

In the tracker's WebUI, **Antenna rotor**:

| Link | Rotor | Tracker |
|---|---|---|
| WiFi (rotctld) | `net.rotctld` on (port 4533) | *Transport* = TCP, *Host* = `APRS-Rotor.local` or IP, *Port* = 4533 |
| Bluetooth SPP (tracker with the original ESP32) | `bt.mode: 2`, `bt.sppProto: 1` | *Transport* = Bluetooth SPP, *BT name* = rotor name |
| App as a bridge | `bt.mode: 1` | *Send the rot event*; the app forwards it with *Follow* |

What it follows (*What the rotor follows*):

- **Satellite** in SAT mode, from a few seconds before AOS until LOS.
- **Station with GO TO**, with its position updated every time it is heard.
- **Fixed target** (repeater, beacon…).
- **Last APRS station heard** (LoRa or FSK), with a callsign filter and a maximum age.
- **Last CW beacon with a locator** (MORSE mode, CW listening).
- **GPredict through the tracker**.

**Moving stations**: the rotor points at the position **estimated now**, worked out from the speed and course of the station's last position over the elapsed time (300 s at most). The name shows `~` while the position is estimated.

## 11. Hamlib programs and PC

- **Over WiFi**: in GPredict, Look4Sat, PstRotator…, Hamlib "NET rotctl" (model 2) with `IP:4533`. Up to 4 clients at once. Test: `rotctl -m 2 -r APRS-Rotor.local:4533 p`.
- **Over USB**: the board presents itself as a controller according to `usb.proto` (GS-232B by default): `rotctl -m 603 -r /dev/ttyUSB0`, `-m 202` (EasyComm II) or `-m 901` (Rot2Prog). With `usb.proto: 5`, USB outputs the debug log.

## 12. Radio and SDR receiver (CAT)

The box also drives a radio or an SDR receiver: frequency, mode, VFO A/B, split, S-meter, CTCSS tone, PTT and satellite Doppler. Details, wiring and models in [CAT.md](CAT.md) (Spanish).

| `cat.proto` | Radios | Link |
|---|---|---|
| 1 Icom CI-V | IC-705, IC-7300, IC-7610, IC-9700, IC-910… (`cat.civ` = address: A2 IC-9700, 94 IC-7300, A4 IC-705) | UART |
| 2 Yaesu | FT-991/A, FT-710, FTDX10, FTDX101, FT-891 | UART |
| 3 Yaesu 5-byte | FT-817/818, FT-857, FT-897 | UART |
| 4 Kenwood / Elecraft | TS-2000, TS-480, TS-590, TS-890, K3, KX2, KX3, QMX | UART |
| 5 SDRconnect | **SDRplay RSPdx-R2** and other RSPs (WebSocket API, port 5454) | WiFi |
| 6 rigctl | SDR++ (4532), gqrx (7356), another rigctld | WiFi |

On the Raspberry Pi, `cat.type: "hamlib"` runs Hamlib's real `rigctld` with any radio (`cat.model` = number from `rigctl -l`).

1. **Wire** the radio to `cat.rx` / `cat.tx` (3.3 V; with RS-232 use a MAX3232; CI-V with its TTL interface). Common ground.
2. **Set** `cat.proto`, `cat.baud` (the radio's), `cat.civ` (Icom) and `cat.model` (display name). For SDRconnect or rigctl: `cat.host` and `cat.port`.
3. **Use** it: **CAT** button on the touch screen, *CAT* card in the WebUI, *Rotor > Radio (CAT)* in the app, or the box's **rigctld** (port 4532) from GPredict, WSJT-X…
4. **Satellites**: with the tracker in SAT mode, the app sends the chosen transmitter and the range rate to the box; the box corrects the downlink (and uplink) Doppler, and on a new satellite sets the mode, split and CTCSS tone (the one typed in the app or the one in the transmitter's description). GPredict can do it directly through the box's rigctld.

**PTT**: off by default (`cat.ptt: false`). When on, it only transmits while held and is released after `cat.pttMaxS` seconds. Receivers (SDRconnect, SDR++) have no PTT.

## 13. Control by APRS messages (LoRa)

On boards with a LoRa module (ESP32 / ESP32-S3 with an external module, Pico with the SX1262 HAT), or on the Raspberry Pi with a KISS TNC. Default radio settings: 433.775 MHz, SF12, BW 125 kHz, CR 4/5 (`lora.*`).

Send an APRS message to the rotor's callsign (`aprs.call`):

| Message | Action |
|---|---|
| `P 123 45` / `AZ 123 EL 45` | go to azimuth 123°, elevation 45° |
| `P 123` | azimuth only |
| `PARK` / `STOP` | park / stop |
| `?` | current position |
| `HELP` | list of commands |

- **Who may send**: the callsigns in `aprs.allow`. `EA7JRS` matches any SSID; with an empty list, only the rotor's base callsign.
- **TOTP code** (optional, `aprs.totp`): a base32 secret from any authenticator app. Append it at the end: `P 123 45 492039`. It cannot be reused and it needs the time (NTP).
- **Replies**: `ack` and `GO AZ 123 EL 45`, `… OK`, `… MOVING` or `ERR …`.
- **Status beacon** every `aprs.beaconMin` minutes.

LoRa APRS is slow: it is not for satellite tracking. More in [APRS_CONTROL.md](APRS_CONTROL.md) (in Spanish).

## 14. Raspberry Pi

```sh
git clone https://github.com/EA7JRS/APRS-LoRa-Plus-Rotor
cd APRS-LoRa-Plus-Rotor/raspberrypi
sudo ./install.sh              # Zero 2 W as a USB GS-232B for the PC: sudo ./install.sh --gadget
```

1. Find your controller in `rotctl -l` (601 GS-232A, 603 GS-232B, 202 EasyComm II, 901 Rot2Prog, 903 MD-01/02, 1 Dummy for testing).
2. In the WebUI (`http://IP:8080`) or in `/etc/aprs-lora-plus-rotor/config.json`:
   ```json
   "hamlib": { "model": 603, "device": "/dev/serial/by-id/...", "speed": 9600 }
   ```
3. Ports: rotctld 4533, JSON 4534, WebUI 8080. Bluetooth: BLE for the app and SPP for the tracker. APRS through a KISS TNC (`aprs.kiss` = `tcp:IP:8001` for a tracker or iGate in KISS mode, or `serial:/dev/ttyACM0:115200`).
4. Log: `journalctl -u aprs-lora-plus-rotor -f`.

More in [RASPBERRY_PI.md](RASPBERRY_PI.md) (in Spanish).

## 15. Safety and security

- **Change the PIN** (`pin`): it protects the app, the WebUI and the JSON protocol. If the WebUI will be reachable from outside your network, also set `net.webPass`.
- **The rotor protects itself**: it stops at the end stops (`limitMargin`), reports a fault on a stall (`stallMs`) and switches everything off if the sensor stops reading. After a fault, a new position command or *Borrar fallo* (clear fault) re-arms it.
- **At power-up** the pins float for a moment: fit 10 kΩ base-to-ground resistors (or use a ULN2003) so the rotor does not move.
- **Wind** (`wind`): `on` (off by default), `park` (gust in m/s to park, 15), `resume` (m/s to resume, 10), `hold` (minutes without strong gusts, 15). While parked the screen, WebUI and app show "WIND" and the wind, and the rotor rejects any movement (STOP and Park still work). Without new data it stays parked; *Borrar fallo* (clear fault) releases it by hand until the next strong gust. The data comes from the internet (the nearest weather station, every 15 min): it does not replace an anemometer on the tower.
- **Radio PTT**: off unless `cat.ptt: true`; only while held and with a time limit (`cat.pttMaxS`).
- **APRS**: callsign list and, better, a TOTP code. The content travels unencrypted, as amateur radio rules require.

## 16. Updates and LED

- **WebUI → Actualizar firmware** (or `http://APRS-Rotor.local/update`): upload the release `firmware.bin`. Not available on 4 MB ESP32 boards; use the web installer.
- **Web installer**: always works and keeps the configuration unless you choose to erase.
- **Version**: all APRS LoRa+ projects share the same number; update the tracker, the rotor and the app together.
- **Status LED** (boards that have one): steady = moving; fast blink = fault; slow flash = idle.

## 17. Troubleshooting

| Symptom | What to do |
|---|---|
| I cannot see the APRS-Rotor WiFi | Wait 30 s after power-up; in client mode, it reopens when it cannot join your network |
| `APRS-Rotor.local` does not answer | Use the IP (from your router or the ⓘ screen); older Android versions do not resolve `.local` |
| It does not move and reports a fault | *Borrar fallo* (clear fault); check the end stops, the stall timer (`stallMs`) and the raw reading |
| It turns the wrong way | Swap `pinA` / `pinB` or recalibrate points A and B |
| The reading jumps | 100 nF at the ADC input, lower `filter`, ADS1115 |
| It overshoots and comes back | Raise `deadband` / `restart`; with PWM, widen `slowZone` |
| The app cannot send commands | PIN differs from the box; *Send PIN* |
| GPredict does not connect | Port 4533, `net.rotctld: true`, same network, firewall |
| The radio does not answer (CAT) | Same baud rate on box and radio (CI-V: not "Auto"), CI-V address, RX/TX crossed, common ground; with SDRconnect, its API on and port 5454 |
| The tracker does not move the rotor | *Antenna rotor* enabled, right transport and host; try `rotctl -m 2 -r IP:4533 p` from a PC |
| APRS replies `ERR no time` | No time for the TOTP: the board needs WiFi with NTP |
| I forgot the PIN | Reinstall with the web installer choosing **erase** (back to factory settings) |

## 18. Reference

| Port | Service |
|---|---|
| 80 (boards) / 8080 (Raspberry Pi) | WebUI and `/update` |
| 4533 | rotctld (Hamlib NET rotctl) |
| 4534 | JSON protocol (app over WiFi) |
| 4532 | radio rigctld (Hamlib NET rigctl, with CAT) |
| BLE `4c6f5261-2b00-4ea7-b000-000000000001` | JSON protocol (app) |

Documents (in Spanish): [CONFIG.md](CONFIG.md) (all options) · [CAT.md](CAT.md) (radios and SDR) · [HARDWARE.md](HARDWARE.md) (wiring) · [CONTROL_PROTOCOL.md](CONTROL_PROTOCOL.md) (JSON protocol) · [APRS_CONTROL.md](APRS_CONTROL.md) · [INTEGRATION.md](INTEGRATION.md) · [RASPBERRY_PI.md](RASPBERRY_PI.md) · [WAVESHARE_C5_LCD35.md](WAVESHARE_C5_LCD35.md) · [WAVESHARE_S3_AMOLED241.md](WAVESHARE_S3_AMOLED241.md) · [WAVESHARE_S3_LCD7B.md](WAVESHARE_S3_LCD7B.md).

**About**: APRS LoRa+ Rotor — by Manuel EA7JRS · © 2026 Manuel - EA7JRS · GPL-3.0-or-later · https://github.com/EA7JRS/APRS-LoRa-Plus. Protocol formats taken from Hamlib; screen font: DejaVu Sans.
