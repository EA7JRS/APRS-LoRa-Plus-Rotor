#!/bin/sh
# APRS LoRa+ Rotor - instalacion en Raspberry Pi OS (Bookworm o posterior): Pi Zero 2 W, 3B / 3B+, 4B / 400 y 5
# Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
#
#   sudo ./install.sh            instala o actualiza y arranca el servicio
#   sudo ./install.sh --gadget   ademas activa el USB "gadget" serie de la Zero 2 W (/dev/ttyGS0): el PC ve un GS-232B por USB
#   sudo ./install.sh --stepper  motores paso a paso con el Waveshare Stepper Motor HAT (python3-lgpio, driver "stepper")
#   sudo ./install.sh --gps      Uputronics GPS/RTC HAT: UART, PPS en GPIO18, reloj RV-3028, gpsd (gps.on); reinicia la Pi
#   sudo ./install.sh --link-uart  UART para una pantalla del rotor por cable (LCD-7 / LCD-7B): GPIO8 TX, GPIO9 RX, GS-232B
#                                  (Pi 4 y 5; en la 3 y la Zero 2 W, adaptador USB-serie); reinicia la Pi
#   sudo ./install.sh --seatel   antena Sea Tel por RS422 (pyserial): seatel_rotctld como servicio en el puerto 4543 (ENSAYO)
#                                puerto con SEATEL_DEV=/dev/ttyXXX: adaptador USB-RS422 (/dev/ttyUSB0, por defecto) o un UART del
#                                propio Pi con un transceptor RS422 (/dev/ttyAMA0, /dev/serial0: activa el UART y quita la consola)
#                                el demonio la usa con driver hamlib model 2 (ver docs/SEATEL.md)
#   (se pueden juntar: sudo ./install.sh --stepper --gps --link-uart; ver docs/RASPBERRY_HATS.md por el GPIO18 de los dos HAT)
#   NO_NTP=1 sudo ./install.sh   sin tocar la hora del sistema (de serie pone NTPsec con la hora del ROA y el GPS de gpsd)
#   PI5_RTC=1 sudo ./install.sh --gps   Pi 5: deja su reloj propio (con su pila) en vez del RV-3028 del Uputronics
set -e

GADGET=""; STEPPER=""; GPS=""; LINK=""; LINKDEV=""; SEATEL=""
for a in "$@"; do
    case "$a" in
        --gadget) GADGET=1 ;;
        --stepper) STEPPER=1 ;;
        --gps) GPS=1 ;;
        --link-uart) LINK=1 ;;
        --seatel) SEATEL=1 ;;
        *) echo "opcion desconocida: $a"; exit 1 ;;
    esac
done
SEATEL_DEV=${SEATEL_DEV:-/dev/ttyUSB0}      # puerto de la PCU Sea Tel (--seatel); cambia con SEATEL_DEV=...

if [ "$(id -u)" != "0" ]; then echo "Ejecuta con sudo"; exit 1; fi
HERE=$(cd "$(dirname "$0")" && pwd)
# Modelo: pi3 (3B / 3B+), pi4, pi5, zero2 u other (aprs_lora_plus_rotor/board.py)
MODEL=$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo Linux)
FAM=$(cd "$HERE" && python3 -c 'import sys; from aprs_lora_plus_rotor import board; print(board.family(sys.argv[1]))' "$MODEL")
echo "== $MODEL ($FAM)"
[ "$FAM" = "other" ] && echo "   aviso: modelo no probado; los de este proyecto son Zero 2 W, 3B / 3B+, 4B / 400 y 5"
DEST=/opt/aprs-lora-plus-rotor
CONF=/etc/aprs-lora-plus-rotor

echo "== Paquetes (Hamlib, D-Bus para Bluetooth)"
apt-get update
apt-get install -y libhamlib-utils python3 python3-dbus-next bluez
[ -n "$STEPPER" ] && apt-get install -y python3-lgpio python3-smbus python3-serial   # lgpio, encoder AS5600 (I2C), TMC2209 (UART)
[ -n "$GPS" ] && apt-get install -y gpsd gpsd-clients pps-tools
[ -n "$SEATEL" ] && apt-get install -y python3-serial      # pyserial para la PCU Sea Tel por RS422

if [ -z "$NO_NTP" ]; then
    echo "== Hora: NTPsec con ntp.roa.es (hora oficial de Espana) y el GPS de gpsd si lo hay"
    apt-get install -y ntpsec
    [ -f /etc/ntpsec/ntp.conf ] && [ ! -f /etc/ntpsec/ntp.conf.orig ] && cp /etc/ntpsec/ntp.conf /etc/ntpsec/ntp.conf.orig
    install -m 644 "$HERE/ntpsec.conf" /etc/ntpsec/ntp.conf
    systemctl restart ntpsec || true
fi

echo "== Programa en $DEST"
mkdir -p "$DEST"
rm -rf "$DEST/aprs_lora_plus_rotor"
cp -r "$HERE/aprs_lora_plus_rotor" "$DEST/"
# La WebUI es la misma pagina que la del firmware
cp "$HERE/../firmware/src/web_page.h" "$DEST/aprs_lora_plus_rotor/web_page.h"

if [ -n "$SEATEL" ]; then
    echo "== Antena Sea Tel: seatel_rotctld como servicio (puerto 4543, modo ENSAYO)"
    cp "$HERE/seatel_rotctld.py" "$DEST/"
    cat > /etc/systemd/system/aprs-lora-plus-seatel.service <<UNIT
[Unit]
Description=seatel_rotctld (PCU Sea Tel como rotctld) para APRS LoRa+ Rotor
After=network.target

[Service]
# De fabrica en ENSAYO (sin --allow-real-motion): no mueve la antena. Cuando lo hayas probado, anade
# --allow-real-motion a esta linea (y --wait-arrival en las primeras pruebas). Ver docs/SEATEL.md.
ExecStart=/usr/bin/python3 $DEST/seatel_rotctld.py --pcu $SEATEL_DEV --port 4543 --log /var/log/seatel.log
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
UNIT
    systemctl daemon-reload
    systemctl enable aprs-lora-plus-seatel
    systemctl restart aprs-lora-plus-seatel || true
    echo "   La PCU esta en $SEATEL_DEV (cambia con SEATEL_DEV=/dev/ttyXXX). Para que el rotor use la Sea Tel,"
    echo "   pon en la configuracion del demonio: \"driver\":\"hamlib\", \"hamlib\":{\"model\":2,\"device\":\"127.0.0.1:4543\"}"
fi

mkdir -p "$CONF"
chmod 700 "$CONF"
if [ ! -f "$CONF/config.json" ]; then
    (cd "$DEST" && python3 -m aprs_lora_plus_rotor.main --print-default) > "$CONF/config.json"
    chmod 600 "$CONF/config.json"
    echo "   configuracion por defecto en $CONF/config.json (modelo Hamlib 1 = Dummy: cambialo por el de tu rotor)"
fi

BOOT=/boot/firmware; [ -d "$BOOT" ] || BOOT=/boot
addcfg() { grep -q "^$1" "$BOOT/config.txt" || echo "$1" >> "$BOOT/config.txt"; }

if [ -n "$GPS" ]; then
    echo "== Uputronics GPS/RTC HAT: UART (GPIO14/15), PPS (GPIO18), reloj RV-3028 (I2C 0x52)"
    addcfg "enable_uart=1"
    addcfg "dtparam=i2c_arm=on"
    addcfg "dtoverlay=pps-gpio,gpiopin=18"
    if [ "$FAM" = "pi5" ]; then
        addcfg "dtparam=uart0=on"                       # en la Pi 5 el UART de los pines 8/10 es ttyAMA0
        GPSDEV=/dev/ttyAMA0
        if [ -z "$PI5_RTC" ]; then
            # La Pi 5 tiene su reloj (rtc0, pila aparte): se apaga para que el RV-3028 del HAT sea el del sistema
            addcfg "dtparam=rtc=off"
            addcfg "dtoverlay=i2c-rtc,rv3028,backup-switchover-mode=1"
        fi
    else
        # Zero 2 W, 3 y 4: el Bluetooth se queda el PL011 y el GPS va por el mini UART (enable_uart=1 fija su reloj)
        addcfg "dtoverlay=i2c-rtc,rv3028,backup-switchover-mode=1"
        GPSDEV=/dev/serial0
    fi
    if command -v raspi-config >/dev/null; then
        raspi-config nonint do_serial_cons 1            # sin consola en el puerto serie
        raspi-config nonint do_serial_hw 0              # con el UART activado
    else
        sed -i 's/console=serial0,[0-9]* //' "$BOOT/cmdline.txt"
    fi
    cat > /etc/default/gpsd <<EOG
# APRS LoRa+ Rotor: u-blox del Uputronics GPS/RTC HAT y su PPS
DEVICES="$GPSDEV /dev/pps0"
GPSD_OPTIONS="-n"
USBAUTO="false"
EOG
    systemctl enable gpsd
    systemctl restart gpsd || true
    REBOOT=1
fi

if [ -n "$LINK" ]; then
    # El UART0 (GPIO14/15) es el del GPS: la pantalla va por otro PL011 en GPIO8 (TX, pin 24) y GPIO9 (RX, pin 21),
    # libres con los dos HAT. /dev/rotor-link apunta a el (el numero de ttyAMA cambia segun los UART activados)
    echo "== UART para la pantalla del rotor por cable (GPIO8 TX, GPIO9 RX)"
    KERN=""
    case "$FAM" in
        pi5) addcfg "dtoverlay=uart3-pi5"; KERN="1f0003c000.serial" ;;
        pi4) addcfg "dtoverlay=uart4"; KERN="fe201800.serial" ;;
        *) LINKDEV=/dev/ttyUSB0
           echo "   $MODEL no tiene otro UART en el conector: usa un adaptador USB-serie de 3,3 V ($LINKDEV)" ;;
    esac
    if [ -n "$KERN" ]; then
        echo "SUBSYSTEM==\"tty\", KERNELS==\"$KERN\", SYMLINK+=\"rotor-link\"" > /etc/udev/rules.d/90-aprs-lora-plus-rotor-link.rules
        LINKDEV=/dev/rotor-link
    fi
    REBOOT=1
fi

# Sea Tel por un puerto serie del propio Pi (no por un adaptador USB): activa el UART y quita la consola serie
if [ -n "$SEATEL" ]; then
    case "$SEATEL_DEV" in
        /dev/ttyAMA*|/dev/serial*|/dev/ttyS*)
            echo "== Sea Tel por el puerto serie del Pi ($SEATEL_DEV): activo el UART y quito la consola serie"
            addcfg "enable_uart=1"
            [ "$FAM" = "pi5" ] && addcfg "dtparam=uart0=on"     # en la Pi 5, ttyAMA0 = pines 8/10
            if command -v raspi-config >/dev/null; then
                raspi-config nonint do_serial_cons 1
                raspi-config nonint do_serial_hw 0
            else
                sed -i 's/console=serial0,[0-9]* //' "$BOOT/cmdline.txt"
            fi
            echo "   AVISO: el UART del Pi es TTL 3,3 V y la PCU Sea Tel es RS422: pon un transceptor TTL<->RS422 en medio (9600 8N1)."
            echo "   AVISO: no uses el mismo UART que el GPS (--gps: ttyAMA0 / serial0) ni la pantalla por cable (--link-uart)."
            echo "          En la Pi 5 cada UART (uart0..4) va a unos pines; elige uno libre y pon SEATEL_DEV a su /dev/ttyAMAx."
            REBOOT=1 ;;
    esac
fi

# Configuracion: driver "stepper" y GPS propio (si se ha pedido); con los dos HAT, el STEP del motor 2 no puede ser el GPIO18
if [ -n "$STEPPER$GPS$LINKDEV" ]; then
    STEPPER="$STEPPER" GPS="$GPS" LINKDEV="$LINKDEV" python3 - "$CONF/config.json" <<'EOP'
import json, os, sys
p = sys.argv[1]
d = json.load(open(p))
if os.environ.get("STEPPER"):
    d["driver"] = "stepper"
if os.environ.get("GPS"):
    d.setdefault("gps", {})["on"] = True
    st = d.setdefault("stepper", {}).setdefault("el", {})
    if d.get("driver") == "stepper" and st.get("step", 18) == 18:
        st["step"] = 26                                  # el cable del STEP del motor 2 al GPIO26 (docs/RASPBERRY_HATS.md)
        print("   stepper.el.step = 26 (el GPIO18 es el PPS del GPS)")
dev = os.environ.get("LINKDEV")
if dev and not any(sp.get("device") == dev for sp in d.setdefault("serialPorts", [])):
    d["serialPorts"].append({"device": dev, "baud": 9600, "proto": 1})   # GS-232B: la pantalla, driver 1 GS-232B 9600
    print("   serialPorts: " + dev + " GS-232B 9600")
json.dump(d, open(p + ".tmp", "w"), indent=1, ensure_ascii=False)
os.chmod(p + ".tmp", 0o600)
os.replace(p + ".tmp", p)
EOP
fi

if [ -n "$GADGET" ] && [ "$FAM" = "pi3" ]; then
    echo "   --gadget: la Pi 3 no tiene USB OTG (solo la Zero 2 W); se salta"; GADGET=""
fi
if [ -n "$GADGET" ]; then
    echo "== USB gadget serie (Zero 2 W)"
    grep -q "^dtoverlay=dwc2" "$BOOT/config.txt" || echo "dtoverlay=dwc2" >> "$BOOT/config.txt"
    grep -q "modules-load=dwc2,g_serial" "$BOOT/cmdline.txt" || sed -i 's/rootwait/rootwait modules-load=dwc2,g_serial/' "$BOOT/cmdline.txt"
    echo '   añade a serialPorts: {"device":"/dev/ttyGS0","baud":9600,"proto":1}  (reinicia la Pi)'
fi

# Ayudante de actualizacion para el boton "Actualizar" de la WebUI: recuerda el repo y las opciones usadas.
REPO=$(cd "$HERE/.." && pwd)
FLAGS=""
[ -n "$GADGET" ] && FLAGS="$FLAGS --gadget"
[ -n "$STEPPER" ] && FLAGS="$FLAGS --stepper"
[ -n "$GPS" ] && FLAGS="$FLAGS --gps"
[ -n "$LINK" ] && FLAGS="$FLAGS --link-uart"
[ -n "$SEATEL" ] && FLAGS="$FLAGS --seatel"
echo "== Ayudante de actualizacion en $DEST/update.sh"
cat > "$DEST/update.sh" <<EOF
#!/bin/sh
# Generado por install.sh: actualiza el rotor desde git y reinstala (lo lanza el boton "Actualizar" de la WebUI).
set -e
cd "$REPO"
git config --global --add safe.directory "$REPO" 2>/dev/null || true
git pull --ff-only
${NO_NTP:+NO_NTP=1 }${PI5_RTC:+PI5_RTC=1 }sh "$REPO/raspberrypi/install.sh"$FLAGS
EOF
chmod +x "$DEST/update.sh"

echo "== Servicio"
install -m 644 "$HERE/aprs-lora-plus-rotor.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable aprs-lora-plus-rotor
systemctl restart aprs-lora-plus-rotor
sleep 2
systemctl --no-pager status aprs-lora-plus-rotor | head -n 12
IP=$(hostname -I | awk '{print $1}')
echo
echo "Listo. WebUI: http://$IP:8080  (usuario admin, contraseña = PIN 123456 hasta que lo cambies)"
echo "rotctld (Hamlib NET rotctl, modelo 2): $IP:4533   ·   modelos de Hamlib: rotctl -l"
[ -z "$NO_NTP" ] && echo "Hora: ntpq -p (servidores del ROA; GPS y PPS si gpsd los da)"
[ -n "$STEPPER" ] && echo "Motores: ajusta stepper.az / stepper.el (micropasos de los DIP, gear, dps) en la configuracion"
[ -n "$GPS" ] && echo "GPS: reinicia la Pi (sudo reboot) para el UART, el PPS y el reloj; luego: cgps, ppstest /dev/pps0"
[ -n "$LINKDEV" ] && echo "Pantalla por cable: $LINKDEV (GS-232B 9600); en la pantalla, driver 1, serial.proto 1, serial.baud 9600"
[ "$FAM" = "pi5" ] && [ -n "$STEPPER" ] && echo "Pi 5 alimentada por el Stepper HAT (3 A): los USB quedan en 600 mA y avisa al arrancar (docs/RASPBERRY_HATS.md)"
[ -n "$REBOOT" ] && echo "Reinicia la Pi: sudo reboot"
