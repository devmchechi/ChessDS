#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "font.h"
#include "gfx.h"
#include "lobby.h"
#include "ui.h"

#define MAX_ITEMS   10
#define MAX_FRIENDS 8
#define ITEMS_Y     42
#define COUNT(a)    (sizeof(a) / sizeof((a)[0]))

typedef enum {
    MODE_MENU,      // browsing menus
    MODE_WAIT,      // seek or challenge sent, waiting for an opponent
    MODE_CHALLENGE, // someone challenged us
} Mode;

typedef enum {
    MENU_MAIN,
    MENU_SEEK_TIME,
    MENU_AI_LEVEL,
    MENU_AI_TIME,
    MENU_FRIENDS,
    MENU_FRIEND_TIME,
} Menu;

typedef struct {
    uint8_t minutes;
    uint8_t increment;
} TimeControl;

// The Board API only allows rapid or slower games against random opponents.
static const TimeControl seek_times[] = { {10, 0}, {10, 5}, {15, 10}, {30, 0}, {30, 20} };
// Blitz is allowed against Stockfish and friends.
static const TimeControl casual_times[] = {
    {3, 2}, {5, 0}, {5, 3}, {10, 0}, {10, 5}, {15, 10}, {30, 0},
};

static Mode mode;
static Menu menu;
static int cursor;
static bool rated;
static int ai_level = 1;
static int friend_choice;
static char friends[MAX_FRIENDS][NAME_LEN];
static int friend_count;
static bool seeking;
static char wait_text[64];
static bool dirty;

static struct {
    bool present;
    char id[16];
    char name[NAME_LEN];
    char rating[8];
    char time_control[12];
    bool rated;
    char my_color; // 'w', 'b' or 'r' (random)
} incoming;

static char title[64];
static char subtitle[64];
static char items[MAX_ITEMS][32];
static int item_count;

static void settle_mode(void)
{
    if (seeking)
        mode = MODE_WAIT;
    else if (incoming.present)
        mode = MODE_CHALLENGE;
    else
        mode = MODE_MENU;

    if (cursor >= MAX_ITEMS)
        cursor = 0;
    dirty = true;
}

void lobby_reset(void)
{
    mode = MODE_MENU;
    menu = MENU_MAIN;
    cursor = 0;
    seeking = false;
    incoming.present = false;
    friend_count = 0;
    dirty = true;
}

void lobby_enter(void)
{
    settle_mode();
}

void lobby_game_started(void)
{
    seeking = false;
    menu = MENU_MAIN;
    cursor = 0;
    settle_mode();
}

static const char *speed_name(const TimeControl *tc)
{
    int estimate = tc->minutes * 60 + tc->increment * 40;
    if (estimate < 180)
        return "Bullet";
    if (estimate < 480)
        return "Blitz";
    if (estimate < 1500)
        return "Rapid";
    return "Classical";
}

static void add_item(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void add_item(const char *fmt, ...)
{
    if (item_count >= MAX_ITEMS)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(items[item_count++], sizeof(items[0]), fmt, args);
    va_end(args);
}

static void add_times(const TimeControl *times, int count)
{
    for (int i = 0; i < count; i++)
        add_item("%2d+%-2d  %s", times[i].minutes, times[i].increment, speed_name(&times[i]));
    add_item("< Back");
}

// Fills title, subtitle and items for the current mode and menu.
static void build(void)
{
    item_count = 0;
    subtitle[0] = '\0';

    if (mode == MODE_WAIT)
    {
        snprintf(title, sizeof(title), "Waiting for an opponent");
        snprintf(subtitle, sizeof(subtitle), "%s", wait_text);
        add_item("Cancel");
        return;
    }
    if (mode == MODE_CHALLENGE)
    {
        const char *color = incoming.my_color == 'w' ? "white"
                            : incoming.my_color == 'b' ? "black" : "random";
        snprintf(title, sizeof(title), "%s (%s) challenges you", incoming.name, incoming.rating);
        snprintf(subtitle, sizeof(subtitle), "%s %s, you play %s", incoming.time_control,
                 incoming.rated ? "rated" : "casual", color);
        add_item("Accept");
        add_item("Decline");
        return;
    }

    switch (menu)
    {
        case MENU_MAIN:
            snprintf(title, sizeof(title), "Play chess on Lichess");
            add_item("Play online (random opponent)");
            add_item("Play vs Stockfish");
            add_item("Challenge a friend");
            add_item("Rated games: %s", rated ? "ON" : "OFF");
            break;
        case MENU_SEEK_TIME:
            snprintf(title, sizeof(title), "Online game: time control");
            snprintf(subtitle, sizeof(subtitle), "%s, rapid or slower", rated ? "Rated" : "Casual");
            add_times(seek_times, COUNT(seek_times));
            break;
        case MENU_AI_LEVEL:
            snprintf(title, sizeof(title), "Stockfish: pick a level");
            for (int level = 1; level <= 8; level++)
                add_item("Level %d", level);
            add_item("< Back");
            break;
        case MENU_AI_TIME:
            snprintf(title, sizeof(title), "Stockfish level %d: time", ai_level);
            snprintf(subtitle, sizeof(subtitle), "Casual, random color");
            add_times(casual_times, COUNT(casual_times));
            break;
        case MENU_FRIENDS:
            snprintf(title, sizeof(title), "Challenge a friend");
            if (friend_count == 0)
                snprintf(subtitle, sizeof(subtitle), "Set CHESSDS_FRIENDS on the Pi");
            for (int i = 0; i < friend_count; i++)
                add_item("%s", friends[i]);
            add_item("< Back");
            break;
        case MENU_FRIEND_TIME:
            snprintf(title, sizeof(title), "Challenge %s", friends[friend_choice]);
            snprintf(subtitle, sizeof(subtitle), "%s", rated ? "Rated" : "Casual");
            add_times(casual_times, COUNT(casual_times));
            break;
    }

    if (cursor >= item_count)
        cursor = item_count - 1;
}

// Entries are up to 28 pixels apart, closer when there are many.
static int item_pitch(void)
{
    int pitch = (SCREEN_H - 4 - ITEMS_Y) / item_count;
    return pitch > 28 ? 28 : pitch;
}

static int item_at(int py)
{
    int index = (py - ITEMS_Y) / item_pitch();
    return py >= ITEMS_Y && index < item_count ? index : -1;
}

static void draw(void)
{
    gfx_target(GFX_BOTTOM);
    gfx_wood(0, 0, SCREEN_W, SCREEN_H);

    font_draw_box(&font_bold, 12, 6, SCREEN_W - 24, 14, ALIGN_LEFT, C_TEXT, title);
    font_draw_box(&font_regular, 12, 22, SCREEN_W - 24, 12, ALIGN_LEFT, C_TEXT_DIM, subtitle);

    int pitch = item_pitch();
    for (int i = 0; i < item_count; i++)
        ui_item(8, ITEMS_Y + i * pitch, SCREEN_W - 16, pitch - 3, i == cursor, items[i]);
}

static void go(Menu next)
{
    menu = next;
    cursor = 0;
    dirty = true;
}

static void go_back(void)
{
    switch (menu)
    {
        case MENU_AI_TIME:
            go(MENU_AI_LEVEL);
            break;
        case MENU_FRIEND_TIME:
            go(MENU_FRIENDS);
            break;
        default:
            go(MENU_MAIN);
            break;
    }
}

static void activate(int index)
{
    dirty = true;

    if (mode == MODE_WAIT)
    {
        app_send("CANCEL");
        return;
    }

    if (mode == MODE_CHALLENGE)
    {
        app_send(index == 0 ? "ACCEPT %s" : "DECLINE %s", incoming.id);
        incoming.present = false;
        settle_mode();
        return;
    }

    if (menu != MENU_MAIN && index == item_count - 1)
    {
        go_back();
        return;
    }

    switch (menu)
    {
        case MENU_MAIN:
            if (index == 0)
                go(MENU_SEEK_TIME);
            else if (index == 1)
                go(MENU_AI_LEVEL);
            else if (index == 2)
                go(MENU_FRIENDS);
            else
                rated = !rated;
            break;
        case MENU_SEEK_TIME:
            app_send("SEEK %d %d %d", seek_times[index].minutes, seek_times[index].increment, rated);
            break;
        case MENU_AI_LEVEL:
            ai_level = index + 1;
            go(MENU_AI_TIME);
            break;
        case MENU_AI_TIME:
            app_send("AI %d %d %d random", ai_level, casual_times[index].minutes,
                     casual_times[index].increment);
            break;
        case MENU_FRIENDS:
            friend_choice = index;
            go(MENU_FRIEND_TIME);
            break;
        case MENU_FRIEND_TIME:
            app_send("CHALLENGE %s %d %d %d", friends[friend_choice], casual_times[index].minutes,
                     casual_times[index].increment, rated);
            break;
    }
}

bool lobby_handle_line(const char *cmd, char *args)
{
    if (strcmp(cmd, "FRIENDS") == 0)
    {
        friend_count = 0;
        char *name;
        while (friend_count < MAX_FRIENDS && (name = next_token(&args)) != NULL)
            snprintf(friends[friend_count++], NAME_LEN, "%s", name);
        if (menu == MENU_FRIENDS || menu == MENU_FRIEND_TIME)
            go(MENU_FRIENDS);
    }
    else if (strcmp(cmd, "SEEKING") == 0)
    {
        snprintf(wait_text, sizeof(wait_text), "%s", args);
        seeking = true;
        cursor = 0;
        settle_mode();
    }
    else if (strcmp(cmd, "IDLE") == 0)
    {
        if (*args != '\0')
            app_log(C_TEXT_DIM, "%s", args);
        if (seeking)
        {
            seeking = false;
            menu = MENU_MAIN;
            cursor = 0;
        }
        settle_mode();
    }
    else if (strcmp(cmd, "CHALLENGED") == 0)
    {
        // CHALLENGED <id> <name> <rating> <time control> <rated> <my color>
        char *id = next_token(&args);
        char *name = next_token(&args);
        char *rating = next_token(&args);
        char *time_control = next_token(&args);
        char *is_rated = next_token(&args);
        char *color = next_token(&args);
        if (color == NULL)
            return true;

        incoming.present = true;
        snprintf(incoming.id, sizeof(incoming.id), "%s", id);
        snprintf(incoming.name, sizeof(incoming.name), "%s", name);
        snprintf(incoming.rating, sizeof(incoming.rating), "%s", rating);
        snprintf(incoming.time_control, sizeof(incoming.time_control), "%s", time_control);
        incoming.rated = is_rated[0] == '1';
        incoming.my_color = color[0];
        app_log(C_CHAT, "Challenge from %s", incoming.name);
        cursor = 0;
        settle_mode();
    }
    else if (strcmp(cmd, "CHALLENGE_GONE") == 0)
    {
        char *id = next_token(&args);
        if (id != NULL && incoming.present && strcmp(id, incoming.id) == 0)
        {
            incoming.present = false;
            app_log(C_TEXT_DIM, "%s cancelled the challenge", incoming.name);
            settle_mode();
        }
    }
    else
    {
        return false;
    }

    return true;
}

void lobby_update(uint32_t keys, const Touch *touch)
{
    build();

    if (touch->down)
    {
        int index = item_at(touch->y);
        if (index >= 0)
        {
            cursor = index;
            activate(index);
        }
    }
    else
    {
        if ((keys & KEY_UP) && cursor > 0)
        {
            cursor--;
            dirty = true;
        }
        if ((keys & KEY_DOWN) && cursor < item_count - 1)
        {
            cursor++;
            dirty = true;
        }
        if (keys & KEY_A)
        {
            activate(cursor);
        }
        else if (keys & KEY_B)
        {
            if (mode == MODE_WAIT)
                app_send("CANCEL");
            else if (mode == MODE_MENU && menu != MENU_MAIN)
                go_back();
        }
    }

    if (dirty)
    {
        dirty = false;
        build();
        draw();
    }
}
