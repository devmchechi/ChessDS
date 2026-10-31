#!/bin/sh
# Turns the Pi's built-in WiFi into a 2.4 GHz network for the DS, configured in
# pi/hotspot.env. The Pi keeps its internet through Ethernet and is 10.42.0.1
# on the new network. Run over Ethernet, since the WiFi connection changes:
#
#     sudo sh ~/chessDS/pi/tools/setup_hotspot.sh
#
# To undo: sudo nmcli connection delete chessds-hotspot
set -eu

cd "$(dirname "$0")/.."

if [ "$(id -u)" != 0 ]; then
    echo "Run this with sudo."
    exit 1
fi
if [ ! -f hotspot.env ]; then
    echo "Create $(pwd)/hotspot.env first (see hotspot.env.example)."
    exit 1
fi
. ./hotspot.env

if ! nmcli -t -f DEVICE,STATE device | grep -q '^eth0:connected$'; then
    echo "Ethernet (eth0) isn't connected: the Pi would lose its internet. Plug it in first."
    exit 1
fi

nmcli connection delete chessds-hotspot >/dev/null 2>&1 || true

# The high priority makes the WiFi start the hotspot at boot instead of joining
# another network. "shared" gives the DS an address by DHCP.
nmcli connection add type wifi ifname wlan0 con-name chessds-hotspot ssid "$HOTSPOT_SSID" \
    connection.autoconnect yes connection.autoconnect-priority 100 \
    802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 6 \
    ipv4.method shared ipv4.addresses 10.42.0.1/24 ipv6.method disabled

if [ -n "$HOTSPOT_PASSWORD" ]; then
    # WPA2 with AES only, and no management frame protection, which older
    # devices like the DSi don't support.
    nmcli connection modify chessds-hotspot \
        wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$HOTSPOT_PASSWORD" \
        wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp wifi-sec.pmf disable
fi

nmcli connection up chessds-hotspot

echo
echo "WiFi network \"$HOTSPOT_SSID\" is up on 2.4 GHz; the Pi is 10.42.0.1 on it."
echo "In chessds.ini on the DS: ssid=$HOTSPOT_SSID, host=10.42.0.1 and the password from hotspot.env."
