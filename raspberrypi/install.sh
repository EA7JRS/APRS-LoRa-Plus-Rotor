#!/bin/sh
# APRS LoRa+ Rotor - instalacion en Raspberry Pi OS (Bookworm o posterior): Pi Zero 2 W, 3, 4 y 5
# Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
#
#   sudo ./install.sh            instala o actualiza y arranca el servicio
#   sudo ./install.sh --gadget   ademas activa el USB "gadget" serie de la Zero 2 W (/dev/ttyGS0): el PC ve un GS-232B por USB
#   sudo ./install.sh --stepper  motores paso a paso con el Waveshare Stepper Motor HAT (python3-lgpio, driver "stepper")
#   sudo ./install.sh --gps      Uputronics GPS/RTC HAT: UART, PPS en GPIO18, reloj RV-3028, gpsd (gps.on); reinicia la Pi
#   (se pueden juntar: sudo ./install.sh --stepper --gps; ver docs/RASPBERRY_HATS.md por el GPIO18 de los dos HAT)
#   NO_NTP=1 sudo ./install.sh   sin tocar la hora del sistema (de serie pone NTPsec con la hora del ROA y el GPS de gpsd)
set -e

GADGET=""; STEPPER=""; GPS=""
for a in "$@"; do
    case "$a" in
        --gadget) GADGET=1 ;;
        --stepper) STEPPER=1 ;;
        --gps) GPS=1 ;;
        *) echo "opcion desconocida: $a"; exit 1 ;;
    esac
done

if [ "$(id -u)" != "0" ]; then echo "Ejecuta con sudo"; exit 1; fi
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=/opt/aprs-lora-plus-rotor
CONF=/etc/aprs-lora-plus-rotor

echo "== Paquetes (Hamlib, D-Bus para Bluetooth)"
apt-get update
apt-get install -y libhamlib-utils python3 python3-dbus-next bluez
[ -n "$STEPPER" ] && apt-get install -y python3-lgpio
[ -n "$GPS" ] && apt-get install -y gpsd gpsd-clients pps-tools

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
    addcfg "dtoverlay=i2c-rtc,rv3028,backup-switchover-mode=1"
    if grep -q "Raspberry Pi 5" /proc/device-tree/model 2>/dev/null; then
        addcfg "dtparam=uart0=on"                       # en la Pi 5 el UART de los pines 8/10 es ttyAMA0
        GPSDEV=/dev/ttyAMA0
    else
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

# Configuracion: driver "stepper" y GPS propio (si se ha pedido); con los dos HAT, el STEP del motor 2 no puede ser el GPIO18
if [ -n "$STEPPER$GPS" ]; then
    STEPPER="$STEPPER" GPS="$GPS" python3 - "$CONF/config.json" <<'EOP'
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
json.dump(d, open(p + ".tmp", "w"), indent=1, ensure_ascii=False)
os.chmod(p + ".tmp", 0o600)
os.replace(p + ".tmp", p)
EOP
fi

if [ -n "$GADGET" ]; then
    echo "== USB gadget serie (Zero 2 W)"
    grep -q "^dtoverlay=dwc2" "$BOOT/config.txt" || echo "dtoverlay=dwc2" >> "$BOOT/config.txt"
    grep -q "modules-load=dwc2,g_serial" "$BOOT/cmdline.txt" || sed -i 's/rootwait/rootwait modules-load=dwc2,g_serial/' "$BOOT/cmdline.txt"
    echo '   añade a serialPorts: {"device":"/dev/ttyGS0","baud":9600,"proto":1}  (reinicia la Pi)'
fi

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
[ -n "$REBOOT" ] && echo "GPS: reinicia la Pi (sudo reboot) para el UART, el PPS y el reloj; luego: cgps, ppstest /dev/pps0"
