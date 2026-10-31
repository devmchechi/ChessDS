#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <nds.h>
#include <dswifi9.h>

#include "app.h"
#include "net.h"

#define SCAN_TIMEOUT    (15 * FPS)
#define ASSOC_TIMEOUT   (30 * FPS)
#define JOIN_ATTEMPTS   5
#define CONNECT_TIMEOUT (8 * FPS)
#define SEND_RETRIES    120

static WifiState wifi_state = WIFI_IDLE;
static const Config *wifi_cfg;
static int wifi_frames;
static int wifi_attempt;
static char wifi_status[64] = "Not started";

static int sock = -1;
static TcpState tcp_state = TCP_CLOSED;
static int tcp_frames;
static char tcp_error[64];
static char rx_buf[4096];
static size_t rx_len;

bool net_init(bool force_ds_mode)
{
    unsigned int flags = INIT_ONLY;
    flags |= force_ds_mode ? WIFI_DS_MODE_ONLY : WIFI_ATTEMPT_DSI_MODE;
    return Wifi_InitDefault(flags);
}

static void wifi_fail(const char *message)
{
    wifi_state = WIFI_FAILED;
    snprintf(wifi_status, sizeof(wifi_status), "%s", message);
}

// Progress message, with the attempt number after the first try.
static void set_status(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(wifi_status, sizeof(wifi_status), fmt, args);
    va_end(args);

    if (wifi_attempt > 1)
    {
        size_t len = strlen(wifi_status);
        snprintf(wifi_status + len, sizeof(wifi_status) - len, " (try %d/%d)", wifi_attempt,
                 JOIN_ATTEMPTS);
    }
}

static void begin_attempt(void)
{
    wifi_frames = 0;

    if (wifi_cfg->ssid[0] == '\0')
    {
        if (Wifi_GetData(WIFIGETDATA_NUMWFCAPS, 0, NULL) <= 0)
        {
            wifi_fail("No ssid in chessds.ini or WFC");
            return;
        }
        Wifi_AutoConnect();
        wifi_state = WIFI_CONNECTING;
        set_status("Using console WiFi settings");
        return;
    }

    Wifi_ScanMode();
    wifi_state = WIFI_SEARCHING;
    set_status("Looking for %s", wifi_cfg->ssid);
}

void net_wifi_connect(const Config *cfg)
{
    if (wifi_state == WIFI_CONNECTING || wifi_state == WIFI_CONNECTED)
        Wifi_DisconnectAP();

    wifi_cfg = cfg;
    wifi_attempt = 1;
    begin_attempt();
}

// Joining often fails for no lasting reason, and the DSi WiFi driver doesn't
// retry by itself, so try a few times before reporting an error.
static void join_failed(const char *message)
{
    if (wifi_attempt >= JOIN_ATTEMPTS)
    {
        wifi_fail(message);
        return;
    }
    Wifi_DisconnectAP();
    wifi_attempt++;
    begin_attempt();
}

static bool find_access_point(const char *ssid, Wifi_AccessPoint *out)
{
    size_t len = strlen(ssid);
    int count = Wifi_GetNumAP();

    for (int i = 0; i < count; i++)
    {
        Wifi_AccessPoint ap;
        Wifi_GetAPData(i, &ap);
        if (ap.ssid_len == len && memcmp(ap.ssid, ssid, len) == 0)
        {
            *out = ap;
            return true;
        }
    }
    return false;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// WEP keys may be written as 10 or 26 hex digits; the library wants bytes.
static size_t wep_key(const char *password, unsigned char *out)
{
    size_t len = strlen(password);
    bool hex = len == 10 || len == 26;
    for (size_t i = 0; hex && i < len; i++)
        hex = hex_value(password[i]) >= 0;

    if (!hex)
    {
        memcpy(out, password, len);
        return len;
    }

    for (size_t i = 0; i < len / 2; i++)
        out[i] = (hex_value(password[2 * i]) << 4) | hex_value(password[2 * i + 1]);
    return len / 2;
}

static void join_access_point(Wifi_AccessPoint *ap)
{
    Wifi_SetIP(0, 0, 0, 0, 0); // use DHCP

    if (ap->security_type == AP_SECURITY_OPEN)
    {
        Wifi_ConnectSecureAP(ap, NULL, 0);
    }
    else if (ap->security_type == AP_SECURITY_WEP)
    {
        unsigned char key[32];
        size_t key_len = wep_key(wifi_cfg->password, key);
        Wifi_ConnectSecureAP(ap, key, key_len);
    }
    else
    {
        Wifi_ConnectSecureAP(ap, wifi_cfg->password, strlen(wifi_cfg->password));
    }
}

WifiState net_wifi_update(void)
{
    wifi_frames++;

    switch (wifi_state)
    {
        case WIFI_SEARCHING:
        {
            Wifi_AccessPoint ap;
            if (!find_access_point(wifi_cfg->ssid, &ap))
            {
                if (wifi_frames > SCAN_TIMEOUT)
                    wifi_fail("Network not found");
                break;
            }

            if (!(ap.flags & WFLAG_APDATA_COMPATIBLE))
            {
                // Original DS mode only supports open and WEP networks.
                wifi_fail(isDSiMode() ? "Unsupported security (WPA1?)"
                                      : "WPA needs DSi mode: use open/WEP");
                break;
            }

            join_access_point(&ap);
            wifi_state = WIFI_CONNECTING;
            wifi_frames = 0;
            set_status("Joining %s", wifi_cfg->ssid);
            break;
        }

        case WIFI_CONNECTING:
        {
            int status = Wifi_AssocStatus();
            if (status == ASSOCSTATUS_ASSOCIATED)
            {
                wifi_state = WIFI_CONNECTED;
                snprintf(wifi_status, sizeof(wifi_status), "Connected");
            }
            else if (status == ASSOCSTATUS_CANNOTCONNECT || wifi_frames > ASSOC_TIMEOUT)
            {
                // The driver can't tell a wrong password from a weak signal.
                char message[64];
                snprintf(message, sizeof(message), "Can't join %s (signal/password)", wifi_cfg->ssid);
                join_failed(message);
            }
            else
            {
                set_status("%s", ASSOCSTATUS_STRINGS[status]);
            }
            break;
        }

        case WIFI_CONNECTED:
            if (Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED)
                wifi_fail("WiFi connection lost");
            break;

        default:
            break;
    }

    return wifi_state;
}

const char *net_wifi_status(void)
{
    return wifi_status;
}

static void tcp_fail(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(tcp_error, sizeof(tcp_error), fmt, args);
    va_end(args);

    net_tcp_close();
}

bool net_tcp_connect(const char *host, int port)
{
    net_tcp_close();
    tcp_error[0] = '\0';

    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    if (getaddrinfo(host, port_text, &hints, &result) != 0 || result == NULL)
    {
        tcp_fail("Can't resolve %s", host);
        return false;
    }

    sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock < 0)
    {
        freeaddrinfo(result);
        tcp_fail("Can't create socket");
        return false;
    }

    int one = 1;
    ioctl(sock, FIONBIO, (char *)&one);
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    // Non-blocking connect: net_tcp_update() polls for the result so the UI
    // keeps running while we wait.
    int rc = connect(sock, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if (rc != 0 && errno != EINPROGRESS && errno != EWOULDBLOCK)
    {
        tcp_fail("Can't connect (error %d)", errno);
        return false;
    }

    tcp_state = rc == 0 ? TCP_CONNECTED : TCP_CONNECTING;
    tcp_frames = 0;
    rx_len = 0;
    return true;
}

TcpState net_tcp_update(void)
{
    if (tcp_state != TCP_CONNECTING)
        return tcp_state;

    struct pollfd pfd = { .fd = sock, .events = POLLOUT, .revents = 0 };
    int ready = poll(&pfd, 1, 0);

    if (ready < 0)
    {
        tcp_fail("poll() failed");
    }
    else if (ready > 0)
    {
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);

        if (error == ECONNREFUSED)
            tcp_fail("Connection refused (bridge running?)");
        else if (error != 0 || (pfd.revents & (POLLERR | POLLNVAL)))
            tcp_fail("Can't connect (error %d)", error);
        else
            tcp_state = TCP_CONNECTED;
    }
    else if (++tcp_frames > CONNECT_TIMEOUT)
    {
        tcp_fail("Timed out (check host/port)");
    }

    return tcp_state;
}

int net_tcp_read_line(char *out, size_t size)
{
    if (tcp_state != TCP_CONNECTED)
        return -1;

    while (true)
    {
        char *newline = memchr(rx_buf, '\n', rx_len);
        if (newline != NULL)
        {
            size_t len = newline - rx_buf;
            size_t copy = len < size - 1 ? len : size - 1;
            memcpy(out, rx_buf, copy);
            out[copy] = '\0';
            if (copy > 0 && out[copy - 1] == '\r')
                out[copy - 1] = '\0';

            rx_len -= len + 1;
            memmove(rx_buf, newline + 1, rx_len);
            return 1;
        }

        // A line that doesn't fit in the buffer is dropped.
        if (rx_len == sizeof(rx_buf))
            rx_len = 0;

        int received = recv(sock, rx_buf + rx_len, sizeof(rx_buf) - rx_len, 0);
        if (received > 0)
        {
            rx_len += received;
            continue;
        }
        if (received == 0)
        {
            tcp_fail("The Pi closed the connection");
            return -1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;

        tcp_fail("Connection lost (error %d)", errno);
        return -1;
    }
}

bool net_tcp_send(const char *line)
{
    if (tcp_state != TCP_CONNECTED)
        return false;

    char buffer[512];
    int total = snprintf(buffer, sizeof(buffer), "%s\n", line);
    if (total < 0 || total >= (int)sizeof(buffer))
        return false;

    int sent = 0;
    int retries = 0;
    while (sent < total)
    {
        int result = send(sock, buffer + sent, total - sent, 0);
        if (result > 0)
        {
            sent += result;
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && retries++ < SEND_RETRIES)
        {
            cothread_yield();
            continue;
        }
        tcp_fail("Send failed");
        return false;
    }
    return true;
}

void net_tcp_close(void)
{
    if (sock >= 0)
    {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        sock = -1;
    }
    tcp_state = TCP_CLOSED;
    rx_len = 0;
}

const char *net_tcp_error(void)
{
    return tcp_error;
}
