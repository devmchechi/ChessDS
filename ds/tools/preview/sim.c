// Runs the real ChessDS code (main.c, game.c, lobby.c, gfx.c, ...) on a PC.
// WiFi, the Pi connection, the screens and the buttons are simulated, driven
// by a script read from stdin, one directive per line:
//
//   wait N        let N frames pass
//   pi LINE       the Pi sends LINE
//   touch X Y     tap the bottom screen
//   key NAME      press A, B, X, L, R, UP, DOWN, LEFT or RIGHT
//   dump NAME     save the screens to NAME.screen (screens.py makes PNGs)
//   expect LINE   check the last line the DS sent to the Pi
//
// Built and run by screens.py.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nds.h>

#include "config.h"
#include "gfx.h"
#include "net.h"

// "VRAM" of the two screens (256x256 bitmaps, 192 lines shown).
static uint16_t vram[2][256 * 256];

static uint32_t keys;
static int touch_x, touch_y;
static int wait_frames;
static int failures;
static char last_sent[512];

// Video and input

int bgInit(int layer, int type, int size, int map_base, int tile_base) { return 0; }
int bgInitSub(int layer, int type, int size, int map_base, int tile_base) { return 1; }
uint16_t *bgGetGfxPtr(int id) { return vram[id]; }
uint32_t keysDown(void) { return keys; }
uint32_t keysDownRepeat(void) { return keys; }
uint32_t keysHeld(void) { return 0; }

void touchRead(touchPosition *touch)
{
    touch->px = touch_x;
    touch->py = touch_y;
}

// config.h

const char *config_load(Config *cfg)
{
    snprintf(cfg->ssid, sizeof(cfg->ssid), "ChessDS");
    cfg->password[0] = '\0';
    snprintf(cfg->host, sizeof(cfg->host), "raspberrypi");
    cfg->port = 5555;
    snprintf(cfg->secret, sizeof(cfg->secret), "changeme");
    return "/chessds.ini";
}

// net.h
// WiFi connects after a moment, then the Pi connection; lines from "pi"
// directives are queued for the DS to read.

static WifiState wifi_state = WIFI_IDLE;
static int wifi_frames;
static char wifi_status[64];
static TcpState tcp_state = TCP_CLOSED;
static int tcp_frames;
static char queue[64][4200];
static int queue_head, queue_tail;

bool net_init(bool force_ds_mode) { return true; }

void net_wifi_connect(const Config *cfg)
{
    wifi_state = WIFI_SEARCHING;
    wifi_frames = 0;
    snprintf(wifi_status, sizeof(wifi_status), "Looking for %s", cfg->ssid);
}

WifiState net_wifi_update(void)
{
    wifi_frames++;
    if (wifi_state == WIFI_SEARCHING && wifi_frames > 40)
    {
        wifi_state = WIFI_CONNECTING;
        snprintf(wifi_status, sizeof(wifi_status), "Joining ChessDS");
    }
    else if (wifi_state == WIFI_CONNECTING && wifi_frames > 80)
    {
        wifi_state = WIFI_CONNECTED;
        snprintf(wifi_status, sizeof(wifi_status), "Connected");
    }
    return wifi_state;
}

const char *net_wifi_status(void) { return wifi_status; }

bool net_tcp_connect(const char *host, int port)
{
    tcp_state = TCP_CONNECTING;
    tcp_frames = 0;
    return true;
}

TcpState net_tcp_update(void)
{
    if (tcp_state == TCP_CONNECTING && ++tcp_frames > 20)
        tcp_state = TCP_CONNECTED;
    return tcp_state;
}

int net_tcp_read_line(char *out, size_t size)
{
    if (tcp_state != TCP_CONNECTED)
        return -1;
    if (queue_head == queue_tail)
        return 0;
    snprintf(out, size, "%s", queue[queue_head]);
    queue_head = (queue_head + 1) % 64;
    return 1;
}

bool net_tcp_send(const char *line)
{
    if (tcp_state != TCP_CONNECTED)
        return false;
    snprintf(last_sent, sizeof(last_sent), "%s", line);
    if (strcmp(line, "PING") != 0)
        fprintf(stderr, "  DS> %s\n", line);
    return true;
}

void net_tcp_close(void) { tcp_state = TCP_CLOSED; }
const char *net_tcp_error(void) { return "Connection refused"; }

// The script

static void dump(const char *name)
{
    char path[4300];
    snprintf(path, sizeof(path), "%s.screen", name);
    FILE *file = fopen(path, "wb");
    if (file == NULL)
    {
        perror(path);
        exit(1);
    }
    // Show what has been drawn so far (the DS would do this after the next VBlank).
    gfx_flush();
    for (int s = 0; s < 2; s++)
        fwrite(vram[s], 2, SCREEN_W * SCREEN_H, file);
    fclose(file);
}

static uint32_t key_named(const char *name)
{
    static const struct { const char *name; uint32_t key; } names[] = {
        { "A", KEY_A }, { "B", KEY_B }, { "X", KEY_X }, { "Y", KEY_Y }, { "L", KEY_L },
        { "R", KEY_R }, { "UP", KEY_UP }, { "DOWN", KEY_DOWN }, { "LEFT", KEY_LEFT },
        { "RIGHT", KEY_RIGHT }, { "START", KEY_START }, { "SELECT", KEY_SELECT },
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        if (strcmp(name, names[i].name) == 0)
            return names[i].key;
    }
    fprintf(stderr, "unknown key %s\n", name);
    exit(1);
}

// main.c calls this once per frame: run the script up to the next directive
// that needs a frame to pass.
void cothread_yield_irq(uint32_t flag)
{
    keys = 0;
    if (wait_frames > 0)
    {
        wait_frames--;
        return;
    }

    char line[4200];
    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (strncmp(line, "wait ", 5) == 0)
        {
            wait_frames = atoi(line + 5) - 1;
            return;
        }
        if (strncmp(line, "pi ", 3) == 0)
        {
            snprintf(queue[queue_tail], sizeof(queue[0]), "%s", line + 3);
            queue_tail = (queue_tail + 1) % 64;
        }
        else if (sscanf(line, "touch %d %d", &touch_x, &touch_y) == 2)
        {
            keys = KEY_TOUCH;
            return;
        }
        else if (strncmp(line, "key ", 4) == 0)
        {
            keys = key_named(line + 4);
            return;
        }
        else if (strncmp(line, "dump ", 5) == 0)
        {
            dump(line + 5);
        }
        else if (strncmp(line, "expect ", 7) == 0)
        {
            if (strcmp(last_sent, line + 7) != 0)
            {
                fprintf(stderr, "FAIL: expected the DS to send '%s', last sent '%s'\n",
                        line + 7, last_sent);
                failures++;
            }
        }
        else if (line[0] != '\0' && line[0] != '#')
        {
            fprintf(stderr, "unknown directive: %s\n", line);
            exit(1);
        }
    }

    exit(failures != 0);
}

int ds_main(int argc, char **argv);

int main(int argc, char **argv)
{
    return ds_main(argc, argv);
}
