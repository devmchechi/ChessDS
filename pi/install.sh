#!/bin/sh
# Sets up the bridge on a Raspberry Pi: Python virtualenv, bridge.env and a
# systemd user service. Needs no root, except once to let the service start at
# boot without a login (the script tells you). Run from anywhere: sh pi/install.sh
set -eu

cd "$(dirname "$0")"
DIR="$(pwd)"

python3 -m venv .venv
.venv/bin/pip install --quiet -r requirements.txt

if [ ! -f bridge.env ]; then
    # A random secret; the DS must send the same one (secret= in chessds.ini).
    secret="$(python3 -c 'import secrets; print(secrets.token_urlsafe(12))')"
    sed "s|^CHESSDS_SECRET=.*|CHESSDS_SECRET=$secret|" bridge.env.example > bridge.env
    chmod 600 bridge.env
    echo "Created $DIR/bridge.env - set LICHESS_TOKEN in it."
fi

mkdir -p "$HOME/.config/systemd/user"
sed "s|__DIR__|$DIR|g" chessds-bridge.service > "$HOME/.config/systemd/user/chessds-bridge.service"
systemctl --user daemon-reload
systemctl --user enable chessds-bridge.service

echo
# "Linger" keeps user services running without a login session, from boot.
if loginctl enable-linger "$(id -un)" 2>/dev/null; then
    echo "The bridge will start at boot."
else
    echo "To start the bridge at boot, run once: sudo loginctl enable-linger $(id -un)"
fi
echo "Start it:  systemctl --user restart chessds-bridge"
echo "Its log:   journalctl --user-unit chessds-bridge -f"
