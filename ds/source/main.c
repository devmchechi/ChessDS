// ChessDS: play on Lichess from a Nintendo DS. The DS talks to a bridge running
// on a Raspberry Pi (see ../pi), which talks to the Lichess Board API.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "config.h"
#include "font.h"
#include "game.h"
#include "gfx.h"
#include "lobby.h"
#include "net.h"
#include "pieces.h"
#include "ui.h"

#define PROTOCOL_VERSION 1
#define PING_INTERVAL    (5 * FPS)
#define SERVER_TIMEOUT   (20 * FPS)
#define HELLO_TIMEOUT    (10 * FPS)
#define RETRY_DELAY      (3 * FPS)
#define MAX_LINES_FRAME  64
#define DPAD             (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)

typedef enum {
    CONN_WIFI,        // joining the WiFi network
    CONN_WIFI_FAILED, // waiting for the user to retry
    CONN_TCP,         // connecting to the Pi
    CONN_HELLO,       // connected, waiting for WELCOME
    CONN_READY,       // logged in
    CONN_RETRY,       // waiting a bit before reconnecting to the Pi
    CONN_DENIED,      // the Pi rejected our secret
} ConnState;

uint32_t frame_count;

static Config config;
static const char *config_path;
static ConnState conn;
static uint32_t conn_timer; // when HELLO was sent, or when to retry
static uint32_t last_rx;
static uint32_t last_ping;
static char username[NAME_LEN];

static Screen screen = SCREEN_CONNECT;
static bool screen_changed = true;
static bool home_dirty = true; // top screen of the connect and lobby screens

static LogLine log_lines[LOG_LINES]; // [0] is the newest
static int log_count;
static uint32_t log_version;

// Services for the other modules (app.h)

bool app_online(void)
{
    return conn == CONN_READY;
}

void app_send(const char *fmt, ...)
{
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    // A failed send closes the socket; receive_lines() notices and reconnects.
    net_tcp_send(line);
}

void app_log(uint16_t color, const char *fmt, ...)
{
    memmove(&log_lines[1], &log_lines[0], sizeof(log_lines[0]) * (LOG_LINES - 1));

    va_list args;
    va_start(args, fmt);
    vsnprintf(log_lines[0].text, sizeof(log_lines[0].text), fmt, args);
    va_end(args);
    log_lines[0].color = color;
    log_lines[0].frame = frame_count;

    if (log_count < LOG_LINES)
        log_count++;
    log_version++;
    home_dirty = true;
}

const LogLine *app_log_line(int age)
{
    return age < log_count ? &log_lines[age] : NULL;
}

uint32_t app_log_version(void)
{
    return log_version;
}

void app_set_screen(Screen next)
{
    screen = next;
    screen_changed = true;
}

char *next_token(char **s)
{
    char *p = *s;
    while (*p == ' ')
        p++;
    if (*p == '\0')
    {
        *s = p;
        return NULL;
    }

    char *start = p;
    while (*p != '\0' && *p != ' ')
        p++;
    if (*p != '\0')
        *p++ = '\0';
    while (*p == ' ')
        p++;

    *s = p;
    return start;
}

// Top screen for the connect and lobby screens

static void draw_info_row(int y, const char *label, const char *value)
{
    font_draw_box(&font_regular, 20, y, 64, 16, ALIGN_LEFT, C_TEXT_DIM, label);
    font_draw_box(&font_bold, 86, y, SCREEN_W - 104, 16, ALIGN_LEFT, C_TEXT, value);
}

static void draw_home(void)
{
    gfx_target(GFX_TOP);
    gfx_wood(0, 0, SCREEN_W, SCREEN_H);

    ui_avatar(10, 10, 'N');
    font_draw(&font_large, 46, 6, C_TEXT, "ChessDS");
    font_draw_box(&font_regular, 47, 28, SCREEN_W - 57, 12, ALIGN_LEFT, C_TEXT_DIM,
                  "Lichess on your Nintendo DS");
    if (conn == CONN_READY)
        ui_pill(SCREEN_W - 10, 12, C_GREEN_DARK, "Online");
    else if (conn == CONN_WIFI_FAILED || conn == CONN_DENIED)
        ui_pill(SCREEN_W - 10, 12, C_RED_DARK, "Offline");
    else
        ui_pill(SCREEN_W - 10, 12, COLOR8(150, 104, 30), "Connecting");

    char pi[80];
    snprintf(pi, sizeof(pi), "%s:%d", config.host, config.port);
    ui_card(8, 48, SCREEN_W - 16, 58);
    draw_info_row(52, "WiFi", config.ssid[0] ? config.ssid : "Console settings");
    draw_info_row(68, "Pi", pi);
    draw_info_row(84, "Lichess", conn == CONN_READY ? username : "Not connected");

    // Messages, newest at the bottom
    ui_card(8, 112, SCREEN_W - 16, 54);
    for (int i = 0; i < LOG_LINES; i++)
    {
        const LogLine *line = app_log_line(LOG_LINES - 1 - i);
        if (line != NULL)
            font_draw_box(&font_regular, 18, 115 + i * 16, SCREEN_W - 36, 16, ALIGN_LEFT,
                          line->color, line->text);
    }

    font_draw_box(&font_regular, 8, 172, SCREEN_W - 16, 16, ALIGN_CENTER, C_TEXT_MUTED,
                  "Tap or D-pad + A  -  B back  -  X flip board");
}

// Connect screen (bottom)

static const char *pi_status(void)
{
    static char text[96];

    switch (conn)
    {
        case CONN_WIFI:
        case CONN_WIFI_FAILED:
            return "Waiting for WiFi";
        case CONN_TCP:
            return "Connecting...";
        case CONN_HELLO:
            return "Logging in...";
        case CONN_READY:
            return "Connected";
        case CONN_DENIED:
            return "Wrong secret in chessds.ini";
        case CONN_RETRY:
            snprintf(text, sizeof(text), "%s - retry in %lus", net_tcp_error(),
                     (unsigned long)((int32_t)(conn_timer - frame_count) / FPS + 1));
            return text;
    }
    return "";
}

static void draw_step(int y, uint16_t dot, const char *title, const char *detail)
{
    ui_card(8, y, SCREEN_W - 16, 42);
    gfx_dot(24, y + 21, 4, dot);
    font_draw_box(&font_bold, 38, y + 4, SCREEN_W - 56, 16, ALIGN_LEFT, C_TEXT, title);
    font_draw_box(&font_regular, 38, y + 21, SCREEN_W - 56, 16, ALIGN_LEFT, C_TEXT_DIM, detail);
}

static void draw_connect(void)
{
    gfx_target(GFX_BOTTOM);
    gfx_wood(0, 0, SCREEN_W, SCREEN_H);
    font_draw(&font_large, 12, 4, C_TEXT, "Connecting");

    uint16_t wifi_dot = C_AMBER;
    if (conn == CONN_WIFI_FAILED)
        wifi_dot = C_RED;
    else if (conn != CONN_WIFI)
        wifi_dot = C_GREEN;
    draw_step(40, wifi_dot, "WiFi network", net_wifi_status());

    uint16_t pi_dot = C_AMBER;
    if (conn == CONN_WIFI || conn == CONN_WIFI_FAILED)
        pi_dot = C_TEXT_MUTED;
    else if (conn == CONN_RETRY || conn == CONN_DENIED)
        pi_dot = C_RED;
    else if (conn == CONN_READY)
        pi_dot = C_GREEN;
    char title[96];
    snprintf(title, sizeof(title), "Raspberry Pi  %s:%d", config.host, config.port);
    draw_step(88, pi_dot, title, pi_status());

    if (conn == CONN_WIFI_FAILED || conn == CONN_DENIED)
        ui_button(64, 138, SCREEN_W - 128, 26, UI_PRIMARY, "Retry", NULL);

    char settings[64];
    snprintf(settings, sizeof(settings), "Settings: %s", config_path ? config_path : "built-in defaults");
    font_draw_box(&font_regular, 8, 172, SCREEN_W - 16, 16, ALIGN_CENTER, C_TEXT_MUTED, settings);
}

// Connection state machine

static void start_wifi(void)
{
    conn = CONN_WIFI;
    net_wifi_connect(&config);
}

static void retry_later(void)
{
    conn = CONN_RETRY;
    conn_timer = frame_count + RETRY_DELAY;
}

static void start_tcp(void)
{
    if (net_tcp_connect(config.host, config.port))
    {
        conn = CONN_TCP;
    }
    else
    {
        app_log(C_RED, "Pi: %s", net_tcp_error());
        retry_later();
    }
}

static void lose_connection(const char *why)
{
    net_tcp_close();
    app_log(C_RED, "%s", why);
    retry_later();

    // Stay on the board during a game; it shows "Reconnecting".
    if (screen != SCREEN_GAME)
        app_set_screen(SCREEN_CONNECT);
}

static void update_connection(uint32_t keys, const Touch *touch)
{
    ConnState before = conn;
    WifiState wifi = net_wifi_update();
    bool retry_pressed = screen == SCREEN_CONNECT && ((keys & KEY_A) || touch->down);

    switch (conn)
    {
        case CONN_WIFI:
            if (wifi == WIFI_CONNECTED)
            {
                app_log(C_TEXT_DIM, "WiFi connected");
                start_tcp();
            }
            else if (wifi == WIFI_FAILED)
            {
                conn = CONN_WIFI_FAILED;
                app_log(C_RED, "WiFi: %s", net_wifi_status());
            }
            break;

        case CONN_WIFI_FAILED:
            if (retry_pressed)
                start_wifi();
            break;

        case CONN_DENIED:
            if (retry_pressed)
                start_tcp();
            break;

        case CONN_TCP:
        {
            TcpState tcp = net_tcp_update();
            if (tcp == TCP_CONNECTED)
            {
                conn = CONN_HELLO;
                conn_timer = frame_count;
                last_rx = frame_count;
                app_send("HELLO %d %s", PROTOCOL_VERSION, config.secret);
            }
            else if (tcp == TCP_CLOSED)
            {
                app_log(C_RED, "Pi: %s", net_tcp_error());
                retry_later();
            }
            break;
        }

        case CONN_HELLO:
            if (frame_count - conn_timer > HELLO_TIMEOUT)
                lose_connection("The Pi did not answer");
            break;

        case CONN_READY:
            if (frame_count - last_ping >= PING_INTERVAL)
            {
                app_send("PING");
                last_ping = frame_count;
            }
            if (frame_count - last_rx > SERVER_TIMEOUT)
                lose_connection("The Pi stopped answering");
            break;

        case CONN_RETRY:
            if ((int32_t)(frame_count - conn_timer) >= 0)
                start_tcp();
            break;
    }

    // The WiFi connection dropped while we were using it: start over.
    if (wifi == WIFI_FAILED && conn != CONN_WIFI && conn != CONN_WIFI_FAILED)
    {
        net_tcp_close();
        app_log(C_RED, "WiFi: %s", net_wifi_status());
        start_wifi();
        if (screen != SCREEN_GAME)
            app_set_screen(SCREEN_CONNECT);
    }

    if (conn != before)
    {
        home_dirty = true;
        if (screen == SCREEN_GAME)
            game_refresh_top();
    }
}

static void handle_line(char *line)
{
    char *args = line;
    char *cmd = next_token(&args);
    if (cmd == NULL)
        return;

    if (strcmp(cmd, "WELCOME") == 0)
    {
        char *name = next_token(&args);
        snprintf(username, sizeof(username), "%s", name != NULL ? name : "?");
        conn = CONN_READY;
        last_ping = frame_count;
        home_dirty = true;
        game_begin_sync();
        lobby_reset();
        app_log(C_GREEN, "Logged in as %s", username);
    }
    else if (strcmp(cmd, "READY") == 0)
    {
        // End of the state the Pi sends after WELCOME.
        if (!game_end_sync())
            app_set_screen(SCREEN_LOBBY);
        else if (screen == SCREEN_GAME)
            game_refresh_top();
    }
    else if (strcmp(cmd, "DENIED") == 0)
    {
        app_log(C_RED, "Pi refused: %s", args);
        net_tcp_close();
        conn = CONN_DENIED;
        app_set_screen(SCREEN_CONNECT);
    }
    else if (strcmp(cmd, "PONG") == 0)
    {
    }
    else if (strcmp(cmd, "INFO") == 0)
    {
        app_log(C_TEXT_DIM, "%s", args);
    }
    else if (strcmp(cmd, "ERROR") == 0)
    {
        app_log(C_RED, "%s", args);
    }
    else if (strcmp(cmd, "CHAT") == 0)
    {
        char *who = next_token(&args);
        app_log(C_CHAT, "%s: %s", who != NULL ? who : "?", args);
    }
    else if (!game_handle_line(cmd, args))
    {
        lobby_handle_line(cmd, args);
    }
}

static void receive_lines(void)
{
    static char line[4096];

    for (int i = 0; i < MAX_LINES_FRAME && (conn == CONN_HELLO || conn == CONN_READY); i++)
    {
        int result = net_tcp_read_line(line, sizeof(line));
        if (result == 0)
            break;
        if (result < 0)
        {
            lose_connection(net_tcp_error());
            break;
        }
        last_rx = frame_count;
        handle_line(line);
    }
}

static void init_video(void)
{
    // Both screens show a 16-bit bitmap (256x256, of which 192 lines are visible).
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    int top = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bottom = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    gfx_init(bgGetGfxPtr(top), bgGetGfxPtr(bottom));
}

int main(int argc, char **argv)
{
    init_video();
    pieces_init();
    game_reset();
    lobby_reset();

    config_path = config_load(&config);

    keysSetRepeat(20, 6);
    scanKeys();
    bool force_ds_mode = keysHeld() & KEY_L;

    if (!net_init(force_ds_mode))
    {
        app_log(C_RED, "Can't start WiFi!");
        draw_home();
        gfx_flush();
        while (1)
            cothread_yield_irq(IRQ_VBLANK);
    }

    app_log(C_TEXT_DIM, "Settings: %s", config_path ? config_path : "built-in defaults");
    start_wifi();

    while (1)
    {
        cothread_yield_irq(IRQ_VBLANK);
        gfx_flush(); // show what was drawn last frame
        frame_count++;

        scanKeys();
        uint32_t keys = (keysDown() & ~DPAD) | (keysDownRepeat() & DPAD);
        Touch touch = { .down = false };
        if (keysDown() & KEY_TOUCH)
        {
            touchPosition position;
            touchRead(&position);
            touch.down = true;
            touch.x = position.px;
            touch.y = position.py;
        }

        update_connection(keys, &touch);
        receive_lines();

        if (screen_changed)
        {
            screen_changed = false;
            home_dirty = true;
            if (screen == SCREEN_LOBBY)
                lobby_enter();
            else if (screen == SCREEN_GAME)
                game_enter();
            else
                draw_connect();

            // Input that caused the switch shouldn't act on the new screen.
            keys = 0;
            touch.down = false;
        }

        switch (screen)
        {
            case SCREEN_CONNECT:
                if (home_dirty || frame_count % 15 == 0)
                    draw_connect();
                break;
            case SCREEN_LOBBY:
                lobby_update(keys, &touch);
                break;
            case SCREEN_GAME:
                game_update(keys, &touch);
                break;
        }

        if (screen != SCREEN_GAME && home_dirty)
            draw_home();
        home_dirty = false;
    }

    return 0;
}
