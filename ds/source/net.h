// WiFi connection and the TCP line connection to the Pi. Everything is
// non-blocking: call the *_update() functions once per frame.

#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"

typedef enum {
    WIFI_IDLE,
    WIFI_SEARCHING,  // scanning for the configured SSID
    WIFI_CONNECTING, // associating and waiting for DHCP
    WIFI_CONNECTED,
    WIFI_FAILED,
} WifiState;

typedef enum {
    TCP_CLOSED,
    TCP_CONNECTING,
    TCP_CONNECTED,
} TcpState;

// Starts the WiFi library. On a DSi it uses the DSi WiFi driver (WPA2 capable)
// unless force_ds_mode is set.
bool net_init(bool force_ds_mode);

void net_wifi_connect(const Config *cfg);
WifiState net_wifi_update(void);
const char *net_wifi_status(void); // progress or error message

bool net_tcp_connect(const char *host, int port);
TcpState net_tcp_update(void);
// Returns 1 if a line was copied to out, 0 if no full line is available yet,
// and -1 if the connection was lost.
int net_tcp_read_line(char *out, size_t size);
bool net_tcp_send(const char *line);
void net_tcp_close(void);
const char *net_tcp_error(void);

#endif
