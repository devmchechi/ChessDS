// Settings read from chessds.ini on the SD card.

#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    char ssid[33];     // empty: use the WiFi settings saved in the console (WFC)
    char password[65]; // WEP or WPA2 key, empty for open networks
    char host[64];     // IP address or hostname of the Raspberry Pi
    int port;
    char secret[33];   // must match CHESSDS_SECRET on the Pi
} Config;

// Fills cfg with the built-in defaults and then applies chessds.ini if it can
// be found. Returns the path of the file that was loaded, or NULL.
const char *config_load(Config *cfg);

#endif
