#!/bin/sh
# APRS LoRa+ Rotor - instalacion en Raspberry Pi OS (Bookworm o posterior): Pi Zero 2 W, 3, 4 y 5
# Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
#
#   sudo ./install.sh            instala o actualiza y arranca el servicio
#   sudo ./install.sh --gadget   ademas activa el USB "gadget" serie de la Zero 2 W (/dev/ttyGS0): el PC ve un GS-232B por USB
#   NO_NTP=1 sudo ./install.sh   sin tocar la hora del sistema (de serie pone NTPsec con la hora del ROA y el GPS de gpsd)
set -e

if [ "$(id -u)" != "0" ]; then echo "Ejecuta con sudo"; exit 1; fi
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=/opt/aprs-lora-plus-rotor
CONF=/etc/aprs-lora-plus-rotor

echo "== Paquetes (Hamlib, D-Bus para Bluetooth)"
apt-get update
apt-get install -y libhamlib-utils python3 python3-dbus-next bluez

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

if [ "$1" = "--gadget" ]; then
    echo "== USB gadget serie (Zero 2 W)"
    BOOT=/boot/firmware; [ -d "$BOOT" ] || BOOT=/boot
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
