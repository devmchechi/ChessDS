# Raspberry Pi setup

This guide was written for Raspberry Pi OS Bookworm on a Pi 4. Any Linux machine
with Python 3.9+ and NetworkManager works the same way.

## 1. Lichess token

Log in to Lichess and open:

<https://lichess.org/account/oauth/token/create?scopes[]=board:play&scopes[]=challenge:write&description=ChessDS>

Create the token and copy it (it starts with `lip_`). Anyone with this token
can play as you, so keep it on the Pi only.

## 2. Install the bridge

Copy the `pi/` folder to the Pi (for example with `git clone` or `scp -r`), then:

```sh
cd ~/chessDS/pi
sh install.sh
```

(If it complains that `venv` is missing: `sudo apt install -y python3-venv`.)

`install.sh` needs no root. It does three things:

- creates a virtualenv in `pi/.venv` with `requests` and `chess`;
- creates `bridge.env` (readable only by you) with a random `CHESSDS_SECRET`;
- installs `chessds-bridge` as a systemd **user** service that starts at boot.

If the script asks you to, run `sudo loginctl enable-linger $USER` once so the
service starts at boot without anyone logging in.

Then enter your token. The script hides it as you type, saves it and starts
the bridge:

```sh
sh ~/chessDS/pi/tools/set_token.sh
```

The other settings are in `bridge.env` (`nano bridge.env`, then restart):

| Setting | Meaning |
| --- | --- |
| `LICHESS_TOKEN` | The token from step 1 |
| `CHESSDS_SECRET` | Any word without spaces. Put the same value in `secret=` in the DS's `chessds.ini` |
| `CHESSDS_BIND`, `CHESSDS_PORT` | Where the DS connects (default `0.0.0.0:5555`) |
| `CHESSDS_FRIENDS` | Lichess usernames for the DS "Challenge a friend" menu, e.g. `alice,bob` |

Restart it and watch the log:

```sh
systemctl --user restart chessds-bridge
journalctl --user-unit chessds-bridge -f
```

You should see `Logged in to Lichess as <you>` and
`Waiting for the DS on 0.0.0.0:5555`.

To run it by hand instead, stop the service first
(`systemctl --user stop chessds-bridge`). Then use `-v` to log every protocol
line:

```sh
.venv/bin/python -m chessds_bridge --config bridge.env -v
```

### Check it without a DS

From the Pi or any PC on the network:

```sh
python3 tools/fake_ds.py --host 127.0.0.1 --secret <your secret>
```

Then type `AI 1 10 0 random` to start a game against Stockfish, and
`MOVE e2e4` to play. The game also appears on lichess.org.

## 3. Network

The DS needs to reach the Pi's port 5555. Pick one of the two options below.

Every DS model uses **2.4 GHz WiFi only**. If your router broadcasts only on
5 GHz, turn its 2.4 GHz band on (option A) or use option B.

### Option A: your home WiFi (DSi / 3DS in DSi mode, or an open/WEP network)

1. Find the Pi's address with `hostname -I`. Reserve that address for the Pi
   in your router so it doesn't change.
2. In `chessds.ini`, set `ssid` and `password` to your home network and `host`
   to the Pi's address. Alternatively leave `ssid=` empty to use the network
   saved in the console's own WiFi settings.

The ROM has to run in DSi mode for WPA2, for example from TWiLight Menu++. A
DSi with a DS flashcart runs it in DS mode, which supports only open/WEP.

### Option B: a hotspot on the Pi (works with every DS)

The Pi's built-in WiFi becomes a dedicated 2.4 GHz network for the DS, so the
Pi needs internet through **Ethernet**. It doesn't need access to your router's
settings.

1. Create the settings file:
   ```sh
   cd ~/chessDS/pi
   cp hotspot.env.example hotspot.env && chmod 600 hotspot.env
   nano hotspot.env
   ```
   Set a `HOTSPOT_PASSWORD` for WPA2 if the DS is a DSi or 3DS running ChessDS
   in DSi mode. Leave it empty for an open network if it's a DS or DS Lite,
   since they can't do WPA.

2. Connect to the Pi over **Ethernet** (its wired address, not the WiFi one,
   because the WiFi changes) and run:
   ```sh
   sudo sh ~/chessDS/pi/tools/setup_hotspot.sh
   ```

The Pi is now `10.42.0.1` on that network, which is the default built into the
ROM. In `chessds.ini`, set `ssid` and `password` from `hotspot.env`,
`host=10.42.0.1`, and the `secret`. The hotspot comes back at every boot. To go
back to normal WiFi: `sudo nmcli connection delete chessds-hotspot`.

**Lock down an open hotspot (recommended).** The DS only needs DHCP and port
5555. These rules stop anyone else on the hotspot from using the Pi's internet
connection or other services:

```sh
sudo tee /etc/chessds-hotspot.nft >/dev/null <<'EOF'
table inet chessds {
    chain input {
        type filter hook input priority -10; policy accept;
        iifname "wlan0" udp dport { 53, 67 } accept
        iifname "wlan0" tcp dport 5555 accept
        iifname "wlan0" drop
    }
    chain forward {
        type filter hook forward priority -10; policy accept;
        iifname "wlan0" drop
    }
}
EOF
echo 'include "/etc/chessds-hotspot.nft"' | sudo tee -a /etc/nftables.conf
sudo systemctl enable --now nftables
sudo systemctl restart nftables
```

This also blocks SSH over the hotspot. Use Ethernet for SSH.

## Updating

Copy the new `pi/` files over the old ones (keep `bridge.env`), then:

```sh
.venv/bin/pip install -r requirements.txt
systemctl --user restart chessds-bridge
```
