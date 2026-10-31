#!/bin/sh
# Asks for the Lichess API token without showing it, saves it in bridge.env and
# restarts the bridge. Run on the Pi: sh ~/chessDS/pi/tools/set_token.sh
set -eu

cd "$(dirname "$0")/.."

trap 'stty echo' EXIT INT TERM
printf "Lichess token (starts with lip_): "
stty -echo
read -r token
stty echo
echo

case "$token" in
    lip_*) ;;
    *) echo "That doesn't look like a Lichess token."; exit 1 ;;
esac

sed -i "s|^LICHESS_TOKEN=.*|LICHESS_TOKEN=$token|" bridge.env
systemctl --user restart chessds-bridge
echo "Saved. Bridge log:"
sleep 4
journalctl --user-unit chessds-bridge -n 6 --no-pager -o cat
