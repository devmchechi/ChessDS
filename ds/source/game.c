#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nds.h>

#include "app.h"
#include "board.h"
#include "font.h"
#include "game.h"
#include "gfx.h"
#include "lobby.h"
#include "pieces.h"
#include "ui.h"

#define MAX_LEGAL      256
#define MAX_SANS       24
#define BUTTON_COUNT   4
#define BUTTON_H       (SCREEN_H / BUTTON_COUNT)
#define CONFIRM_FRAMES (3 * FPS)
#define MESSAGE_FRAMES (4 * FPS) // how long a new message replaces the status line
#define FRAME_US       16715     // the DS refreshes at ~59.83 Hz

#define PROMO_CELL     28
#define PROMO_W        (4 * PROMO_CELL + 4)
#define PROMO_H        (PROMO_CELL + 4)
#define PROMO_X        ((BOARD_PX - PROMO_W) / 2)
#define PROMO_Y        ((BOARD_PX - PROMO_H) / 2)

// Top screen layout
#define BAR_H          30
#define OPPONENT_Y     0
#define LIST_Y         31
#define LIST_ROW_H     14
#define LIST_ROWS      8
#define LIST_H         (LIST_ROWS * LIST_ROW_H + 4)
#define STATUS_Y       (LIST_Y + LIST_H)
#define ME_Y           (SCREEN_H - BAR_H)
#define CLOCK_X        172
#define CLOCK_W        80
#define CLOCK_H        24
#define NUMBER_X       6   // right-aligned in 32 pixels
#define WHITE_MOVE_X   48
#define BLACK_MOVE_X   148

typedef enum {
    BTN_NONE,
    BTN_RESIGN,
    BTN_ABORT,
    BTN_DRAW,
    BTN_DRAW_SENT,
    BTN_ACCEPT_DRAW,
    BTN_DECLINE_DRAW,
    BTN_FLIP,
    BTN_CLAIM,
    BTN_MENU,
} Button;

static const char promo_letters[4] = { 'q', 'r', 'b', 'n' };

static struct {
    bool loaded;
    bool synced;
    bool over;
    bool waiting; // our move was sent and the Pi hasn't confirmed it yet

    char id[16];
    char my_color; // 'w' or 'b'
    char name[2][NAME_LEN]; // [0] white, [1] black
    char rating[2][8];
    char time_control[12];
    bool rated;

    Board board;
    bool white_to_move;
    Move last;
    bool has_last;
    int32_t clock_ms[2]; // -1 when the game has no clock
    uint32_t clock_frame; // frame when clock_ms was received
    int ply;
    bool check;
    char draw_offer; // color offering a draw: 'w', 'b' or '-'

    Move legal[MAX_LEGAL];
    int legal_count;

    char san[MAX_SANS][8];
    int san_count;
    int first_move_number;
    bool first_is_black;

    char result[64];
    int gone_seconds; // -1: the opponent is connected
    uint32_t gone_frame;
    int gone_shown;

    bool flipped;
    int selected;
    int cursor;
    bool cursor_visible;
    bool promo_open;
    Move promo_move;
    int promo_cursor;
    Button confirm;
    uint32_t confirm_until;

    uint32_t log_version;
    bool message_shown;
    uint32_t loaded_frame; // messages from before the game aren't shown in it

    bool dirty_board;
    bool dirty_panel;
    bool dirty_top;
} g;

static void mark_dirty(void)
{
    g.dirty_board = g.dirty_panel = g.dirty_top = true;
}

void game_reset(void)
{
    memset(&g, 0, sizeof(g));
    g.selected = SQ_NONE;
    g.cursor = SQ_NONE;
    g.gone_seconds = -1;
    g.gone_shown = -1;
    g.draw_offer = '-';
    g.clock_ms[0] = g.clock_ms[1] = -1;
}

bool game_loaded(void)
{
    return g.loaded;
}

void game_begin_sync(void)
{
    g.synced = false;
}

bool game_end_sync(void)
{
    if (!g.synced)
        game_reset();
    g.synced = true;
    return g.loaded;
}

// Helpers

static int color_index(char color)
{
    return color == 'w' ? 0 : 1;
}

static char opponent_color(void)
{
    return g.my_color == 'w' ? 'b' : 'w';
}

static char side_to_move(void)
{
    return g.white_to_move ? 'w' : 'b';
}

static bool my_turn(void)
{
    return g.loaded && !g.over && !g.waiting && app_online() && side_to_move() == g.my_color;
}

static bool has_moves_from(int sq)
{
    for (int i = 0; i < g.legal_count; i++)
    {
        if (g.legal[i].from == sq)
            return true;
    }
    return false;
}

// Number of legal moves between two squares (4 for a promotion). The first
// one is copied to *found.
static int find_moves(int from, int to, Move *found)
{
    int count = 0;
    for (int i = 0; i < g.legal_count; i++)
    {
        if (g.legal[i].from == from && g.legal[i].to == to)
        {
            if (count == 0)
                *found = g.legal[i];
            count++;
        }
    }
    return count;
}

static bool clock_running(int side)
{
    return g.loaded && !g.over && !g.waiting && g.ply >= 2 && g.clock_ms[side] >= 0
           && (side == 0) == g.white_to_move;
}

static int32_t clock_value(int side)
{
    int32_t ms = g.clock_ms[side];
    if (ms < 0 || !clock_running(side))
        return ms;

    uint64_t elapsed = (uint64_t)(frame_count - g.clock_frame) * FRAME_US / 1000;
    return elapsed >= (uint64_t)ms ? 0 : ms - (int32_t)elapsed;
}

static void format_clock(int32_t ms, char *out, size_t size)
{
    if (ms < 0)
    {
        snprintf(out, size, "--:--");
        return;
    }

    unsigned long s = ms / 1000;
    if (s >= 3600)
        snprintf(out, size, "%lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
    else if (ms < 10000)
        snprintf(out, size, "0:%02lu.%lu", s, (unsigned long)(ms % 1000 / 100));
    else
        snprintf(out, size, "%lu:%02lu", s / 60, s % 60);
}

// Seconds until we can claim the win, 0 if we can now, -1 if the opponent is here.
static int gone_remaining(void)
{
    if (g.gone_seconds < 0)
        return -1;
    int elapsed = (frame_count - g.gone_frame) / FPS;
    return elapsed >= g.gone_seconds ? 0 : g.gone_seconds - elapsed;
}

// Replaces piece letters with figurine characters (1-6) for the move list.
static void figurine_san(const char *san, char *out, size_t size)
{
    size_t n = 0;
    for (; *san != '\0' && n + 1 < size; san++)
        out[n++] = strchr("NBRQK", *san) != NULL ? (char)(piece_kind(*san) + 1) : *san;
    out[n] = '\0';
}

static bool message_active(void)
{
    const LogLine *line = app_log_line(0);
    return line != NULL && (int32_t)(line->frame - g.loaded_frame) >= 0
           && frame_count - line->frame < MESSAGE_FRAMES;
}

// Bottom screen

static void draw_promotion(void)
{
    gfx_round(PROMO_X - 3, PROMO_Y - 3, PROMO_W + 6, PROMO_H + 6, COLOR8(44, 30, 21), 16);
    gfx_round(PROMO_X - 3, PROMO_Y - 3, PROMO_W + 6, 1, C_WHITE, 3);

    for (int i = 0; i < 4; i++)
    {
        int x = PROMO_X + 2 + i * PROMO_CELL;
        if (i == g.promo_cursor)
            gfx_round(x, PROMO_Y + 2, PROMO_CELL, PROMO_CELL, C_GREEN, 16);

        char letter = promo_letters[i];
        gfx_piece(x + 2, PROMO_Y + 4, g.my_color == 'w' ? toupper((unsigned char)letter) : letter);
    }
}

static void draw_board(void)
{
    uint64_t targets = 0;
    if (g.selected != SQ_NONE)
    {
        for (int i = 0; i < g.legal_count; i++)
        {
            if (g.legal[i].from == g.selected)
                targets |= 1ULL << g.legal[i].to;
        }
    }

    BoardView view = {
        .board = &g.board,
        .flipped = g.flipped,
        .selected = g.selected,
        .cursor = g.cursor_visible ? g.cursor : SQ_NONE,
        .last_from = g.has_last ? g.last.from : SQ_NONE,
        .last_to = g.has_last ? g.last.to : SQ_NONE,
        .check_square = g.check ? board_find_king(&g.board, g.white_to_move) : SQ_NONE,
        .targets = targets,
    };
    gfx_target(GFX_BOTTOM);
    gfx_board(&view);

    if (g.promo_open)
        draw_promotion();
}

static void panel_buttons(Button out[BUTTON_COUNT])
{
    if (g.over)
    {
        out[0] = BTN_MENU;
        out[1] = BTN_NONE;
        out[2] = BTN_FLIP;
        out[3] = BTN_NONE;
        return;
    }

    out[0] = g.ply < 2 ? BTN_ABORT : BTN_RESIGN;
    out[2] = BTN_FLIP;
    if (g.draw_offer == opponent_color())
    {
        out[1] = BTN_ACCEPT_DRAW;
        out[3] = BTN_DECLINE_DRAW;
    }
    else
    {
        out[1] = g.draw_offer == g.my_color ? BTN_DRAW_SENT : BTN_DRAW;
        out[3] = gone_remaining() == 0 ? BTN_CLAIM : BTN_NONE;
    }
}

static void button_labels(Button button, const char **line1, const char **line2)
{
    switch (button)
    {
        case BTN_RESIGN:       *line1 = "Resign";  *line2 = NULL;    break;
        case BTN_ABORT:        *line1 = "Abort";   *line2 = NULL;    break;
        case BTN_DRAW:         *line1 = "Offer";   *line2 = "draw";  break;
        case BTN_DRAW_SENT:    *line1 = "Draw";    *line2 = "sent";  break;
        case BTN_ACCEPT_DRAW:  *line1 = "Accept";  *line2 = "draw";  break;
        case BTN_DECLINE_DRAW: *line1 = "Refuse";  *line2 = "draw";  break;
        case BTN_FLIP:         *line1 = "Flip";    *line2 = "board"; break;
        case BTN_CLAIM:        *line1 = "Claim";   *line2 = "win";   break;
        case BTN_MENU:         *line1 = "Main";    *line2 = "menu";  break;
        default:               *line1 = "";        *line2 = NULL;    break;
    }
}

static void draw_panel(void)
{
    Button buttons[BUTTON_COUNT];
    panel_buttons(buttons);

    gfx_target(GFX_BOTTOM);
    gfx_wood(PANEL_X, 0, PANEL_W, SCREEN_H);
    gfx_fill(PANEL_X, 0, 1, SCREEN_H, COLOR8(20, 13, 9)); // edge of the board

    for (int i = 0; i < BUTTON_COUNT; i++)
    {
        Button button = buttons[i];
        if (button == BTN_NONE)
            continue;

        UiStyle style = UI_NORMAL;
        const char *line1, *line2;
        button_labels(button, &line1, &line2);

        if (g.confirm == button)
        {
            style = UI_DANGER;
            line1 = "Tap";
            line2 = "again";
        }
        else if (button == BTN_ACCEPT_DRAW || button == BTN_CLAIM)
        {
            style = UI_PRIMARY;
        }
        else if (button == BTN_DRAW_SENT)
        {
            style = UI_DISABLED;
        }

        ui_button(PANEL_X + 5, i * BUTTON_H + 4, PANEL_W - 9, BUTTON_H - 8, style, line1, line2);
    }
}

// Top screen

static void draw_clock(int bar_y, int side)
{
    char text[16];
    int32_t ms = clock_value(side);
    format_clock(ms, text, sizeof(text));

    int y = bar_y + (BAR_H - CLOCK_H) / 2;
    gfx_wood(CLOCK_X, y, CLOCK_W, CLOCK_H);
    ui_clock(CLOCK_X, y, CLOCK_W, CLOCK_H, text, clock_running(side), ms >= 0 && ms < 20000);
}

static void draw_clocks(void)
{
    gfx_target(GFX_TOP);
    draw_clock(OPPONENT_Y, color_index(opponent_color()));
    draw_clock(ME_Y, color_index(g.my_color));
}

static void draw_player_bar(int y, char color)
{
    int i = color_index(color);
    gfx_wood(0, y, SCREEN_W, BAR_H);
    ui_avatar(5, y + 1, color == 'w' ? 'K' : 'k');

    font_draw_box(&font_bold, 40, y + 2, CLOCK_X - 46, 13, ALIGN_LEFT, C_TEXT, g.name[i]);

    char detail[32];
    if (strcmp(g.rating[i], "AI") == 0)
        snprintf(detail, sizeof(detail), "Computer");
    else
        snprintf(detail, sizeof(detail), "%s%s", g.rating[i], color == g.my_color ? "  (you)" : "");
    font_draw_box(&font_regular, 40, y + 16, CLOCK_X - 46, 12, ALIGN_LEFT, C_TEXT_DIM, detail);

    draw_clock(y, i);
}

static void draw_move(int x, int y, const char *san, bool last)
{
    char text[16];
    figurine_san(san, text, sizeof(text));
    if (last)
    {
        int w = font_width(&font_bold, text);
        gfx_round(x - 4, y, w + 8, LIST_ROW_H, C_WHITE, 3);
    }
    font_draw_box(&font_bold, x, y, 90, LIST_ROW_H, ALIGN_LEFT, last ? C_WHITE : C_TEXT, text);
}

static void draw_moves(void)
{
    gfx_wood(0, LIST_Y, SCREEN_W, LIST_H);
    ui_card(4, LIST_Y, SCREEN_W - 8, LIST_H);

    if (g.san_count == 0)
    {
        font_draw_box(&font_regular, 4, LIST_Y, SCREEN_W - 8, LIST_H, ALIGN_CENTER, C_TEXT_MUTED,
                      "No moves yet");
        return;
    }

    // Rows of the move list: [move number, index of white's move, index of black's move].
    int rows[MAX_SANS][3];
    int count = 0;
    int number = g.first_move_number;
    for (int i = 0; i < g.san_count && count < MAX_SANS; number++, count++)
    {
        rows[count][0] = number;
        if (i == 0 && g.first_is_black)
        {
            rows[count][1] = -1;
            rows[count][2] = 0;
            i = 1;
        }
        else
        {
            rows[count][1] = i;
            rows[count][2] = i + 1 < g.san_count ? i + 1 : -1;
            i += 2;
        }
    }

    int first = count > LIST_ROWS ? count - LIST_ROWS : 0;
    for (int r = 0; r < LIST_ROWS && first + r < count; r++)
    {
        const int *row = rows[first + r];
        int y = LIST_Y + 2 + r * LIST_ROW_H;
        if (r % 2 == 1)
            gfx_round(6, y, SCREEN_W - 12, LIST_ROW_H, C_WHITE, 1);

        char number_text[8];
        snprintf(number_text, sizeof(number_text), "%d.", row[0]);
        font_draw_box(&font_regular, NUMBER_X, y, 32, LIST_ROW_H, ALIGN_RIGHT, C_TEXT_MUTED, number_text);

        if (row[1] >= 0)
            draw_move(WHITE_MOVE_X, y, g.san[row[1]], row[1] == g.san_count - 1);
        else
            font_draw_box(&font_bold, WHITE_MOVE_X, y, 90, LIST_ROW_H, ALIGN_LEFT, C_TEXT_MUTED, "...");
        if (row[2] >= 0)
            draw_move(BLACK_MOVE_X, y, g.san[row[2]], row[2] == g.san_count - 1);
    }
}

static void draw_status(void)
{
    gfx_wood(0, STATUS_Y, SCREEN_W, ME_Y - STATUS_Y);

    int gone = gone_remaining();
    uint16_t color = C_TEXT;
    char text[64];
    const LogLine *message = app_log_line(0);

    if (!app_online())
    {
        color = C_RED;
        snprintf(text, sizeof(text), "Reconnecting to the Pi...");
    }
    else if (message_active())
    {
        color = message->color;
        snprintf(text, sizeof(text), "%s", message->text);
    }
    else if (g.over)
    {
        color = C_AMBER;
        snprintf(text, sizeof(text), "%s", g.result);
    }
    else if (g.waiting)
    {
        color = C_TEXT_DIM;
        snprintf(text, sizeof(text), "Sending move...");
    }
    else if (gone == 0)
    {
        color = C_AMBER;
        snprintf(text, sizeof(text), "Opponent left: claim the win");
    }
    else if (gone > 0)
    {
        color = C_AMBER;
        snprintf(text, sizeof(text), "Opponent left (claim in %ds)", gone);
    }
    else if (g.draw_offer == opponent_color())
    {
        color = C_AMBER;
        snprintf(text, sizeof(text), "Your opponent offers a draw");
    }
    else if (side_to_move() == g.my_color)
    {
        color = g.check ? C_RED : C_GREEN;
        snprintf(text, sizeof(text), "%s", g.check ? "Check! Your move" : "Your move");
    }
    else
    {
        color = C_TEXT_DIM;
        snprintf(text, sizeof(text), "Waiting for your opponent");
    }

    // The time control goes on the right when the status leaves room for it.
    char info[24];
    snprintf(info, sizeof(info), "%s %s", g.time_control, g.rated ? "rated" : "casual");
    int info_w = font_width(&font_regular, info);
    int text_w = SCREEN_W - 16;
    int h = ME_Y - STATUS_Y;
    if (font_width(&font_bold, text) <= text_w - info_w - 12)
    {
        font_draw_box(&font_regular, SCREEN_W - 8 - info_w, STATUS_Y, info_w, h, ALIGN_RIGHT,
                      C_TEXT_MUTED, info);
        text_w -= info_w + 12;
    }
    font_draw_box(&font_bold, 8, STATUS_Y, text_w, h, ALIGN_LEFT, color, text);
}

static void draw_top(void)
{
    gfx_target(GFX_TOP);
    gfx_wood(0, 0, SCREEN_W, SCREEN_H); // also clears the gaps between the parts
    draw_player_bar(OPPONENT_Y, opponent_color());
    draw_moves();
    draw_status();
    draw_player_bar(ME_Y, g.my_color);
}

// Messages from the Pi

// GAME <id> <my color> <white> <white rating> <black> <black rating> <time control> <rated>
static void on_game(char *args)
{
    char *id = next_token(&args);
    char *color = next_token(&args);
    char *white = next_token(&args);
    char *white_rating = next_token(&args);
    char *black = next_token(&args);
    char *black_rating = next_token(&args);
    char *time_control = next_token(&args);
    char *rated = next_token(&args);
    if (rated == NULL)
        return;

    // Keep the board orientation when the Pi re-sends the same game.
    bool same_game = g.loaded && strcmp(g.id, id) == 0;
    bool flipped = g.flipped;

    game_reset();
    g.loaded = true;
    g.synced = true;
    snprintf(g.id, sizeof(g.id), "%s", id);
    g.my_color = color[0] == 'b' ? 'b' : 'w';
    snprintf(g.name[0], sizeof(g.name[0]), "%s", white);
    snprintf(g.rating[0], sizeof(g.rating[0]), "%s", white_rating);
    snprintf(g.name[1], sizeof(g.name[1]), "%s", black);
    snprintf(g.rating[1], sizeof(g.rating[1]), "%s", black_rating);
    snprintf(g.time_control, sizeof(g.time_control), "%s", time_control);
    g.rated = rated[0] == '1';
    g.flipped = same_game ? flipped : g.my_color == 'b';
    g.white_to_move = true;
    g.log_version = app_log_version();
    g.loaded_frame = frame_count + 1; // ignore messages logged this frame, too
    board_from_placement(&g.board, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR");

    lobby_game_started();
    app_set_screen(SCREEN_GAME);
}

// STATE <placement> <turn> <last move|-> <wtime> <btime> <ply> <check> <draw offer>
static void on_state(char *args)
{
    char *placement = next_token(&args);
    char *turn = next_token(&args);
    char *last = next_token(&args);
    char *wtime = next_token(&args);
    char *btime = next_token(&args);
    char *ply = next_token(&args);
    char *check = next_token(&args);
    char *draw = next_token(&args);
    if (draw == NULL)
        return;

    Board board;
    if (!board_from_placement(&board, placement))
        return;

    g.board = board;
    g.white_to_move = turn[0] == 'w';
    g.has_last = strcmp(last, "-") != 0 && move_parse(&g.last, last);
    g.clock_ms[0] = atol(wtime);
    g.clock_ms[1] = atol(btime);
    g.clock_frame = frame_count;
    g.ply = atoi(ply);
    g.check = check[0] == '1';
    g.draw_offer = draw[0];

    g.waiting = false;
    g.promo_open = false;
    g.legal_count = 0; // LEGAL follows

    mark_dirty();
}

// LEGAL <uci> <uci> ... (empty when it isn't our turn)
static void on_legal(char *args)
{
    g.legal_count = 0;

    char *token;
    while (g.legal_count < MAX_LEGAL && (token = next_token(&args)) != NULL)
    {
        if (move_parse(&g.legal[g.legal_count], token))
            g.legal_count++;
    }

    if (g.selected != SQ_NONE && !has_moves_from(g.selected))
        g.selected = SQ_NONE;

    g.dirty_board = true;
    g.dirty_top = true;
}

// MOVES <number of the first move> <color of the first move> <san> <san> ...
static void on_moves(char *args)
{
    char *number = next_token(&args);
    char *color = next_token(&args);
    if (color == NULL)
        return;

    g.first_move_number = atoi(number);
    g.first_is_black = color[0] == 'b';
    g.san_count = 0;

    char *token;
    while (g.san_count < MAX_SANS && (token = next_token(&args)) != NULL)
        snprintf(g.san[g.san_count++], sizeof(g.san[0]), "%s", token);

    g.dirty_top = true;
}

// END <status> <winner w|b|-> <text>
static void on_end(char *args)
{
    char *status = next_token(&args);
    char *winner = next_token(&args);
    if (winner == NULL)
        return;

    g.over = true;
    g.legal_count = 0;
    g.selected = SQ_NONE;
    g.promo_open = false;
    g.confirm = BTN_NONE;
    g.gone_seconds = -1;
    snprintf(g.result, sizeof(g.result), "%s", *args != '\0' ? args : status);
    app_log(C_AMBER, "%s", g.result);

    mark_dirty();
}

// GONE <seconds until a win can be claimed> | GONE back
static void on_gone(char *args)
{
    char *value = next_token(&args);
    if (value == NULL)
        return;

    if (strcmp(value, "back") == 0)
    {
        g.gone_seconds = -1;
        app_log(C_TEXT_DIM, "Your opponent is back");
    }
    else
    {
        g.gone_seconds = atoi(value);
        g.gone_frame = frame_count;
        app_log(C_AMBER, "Your opponent left the game");
    }

    g.dirty_panel = true;
    g.dirty_top = true;
}

bool game_handle_line(const char *cmd, char *args)
{
    if (strcmp(cmd, "GAME") == 0)
    {
        on_game(args);
        return true;
    }

    void (*handler)(char *) = NULL;
    if (strcmp(cmd, "STATE") == 0)
        handler = on_state;
    else if (strcmp(cmd, "LEGAL") == 0)
        handler = on_legal;
    else if (strcmp(cmd, "MOVES") == 0)
        handler = on_moves;
    else if (strcmp(cmd, "END") == 0)
        handler = on_end;
    else if (strcmp(cmd, "GONE") == 0)
        handler = on_gone;
    else
        return false;

    if (g.loaded)
        handler(args);
    return true;
}

// Input

static void send_move(const Move *move)
{
    char uci[6];
    move_format(move, uci);
    app_send("MOVE %s", uci);

    // Freeze our clock and show the move right away; the Pi's STATE replaces
    // this (or undoes it if Lichess refused the move).
    int me = color_index(g.my_color);
    g.clock_ms[me] = clock_value(me);
    g.clock_frame = frame_count;

    board_apply(&g.board, move);
    g.last = *move;
    g.has_last = true;
    g.check = false;
    g.waiting = true;
    g.selected = SQ_NONE;
    g.legal_count = 0;
    g.promo_open = false;

    mark_dirty();
}

static void board_click(int sq)
{
    if (sq == SQ_NONE || !my_turn())
        return;

    if (g.selected != SQ_NONE)
    {
        Move move;
        int count = find_moves(g.selected, sq, &move);
        if (count == 1)
        {
            send_move(&move);
            return;
        }
        if (count > 1)
        {
            // Promotion: ask which piece.
            g.promo_open = true;
            g.promo_move = move;
            g.promo_cursor = 0;
            g.dirty_board = true;
            return;
        }
    }

    g.selected = (sq != g.selected && has_moves_from(sq)) ? sq : SQ_NONE;
    g.dirty_board = true;
}

static void promotion_input(uint32_t keys, const Touch *touch)
{
    int choice = -1;

    if (touch->down)
    {
        int x = touch->x - PROMO_X - 2;
        int y = touch->y - PROMO_Y;
        if (x >= 0 && x < 4 * PROMO_CELL && y >= 0 && y < PROMO_H)
            choice = x / PROMO_CELL;
        else
            keys |= KEY_B; // touching outside cancels
    }

    if ((keys & KEY_LEFT) && g.promo_cursor > 0)
    {
        g.promo_cursor--;
        g.dirty_board = true;
    }
    if ((keys & KEY_RIGHT) && g.promo_cursor < 3)
    {
        g.promo_cursor++;
        g.dirty_board = true;
    }
    if (keys & KEY_A)
        choice = g.promo_cursor;

    if (choice >= 0)
    {
        Move move = g.promo_move;
        move.promo = promo_letters[choice];
        send_move(&move);
    }
    else if (keys & KEY_B)
    {
        g.promo_open = false;
        g.selected = SQ_NONE;
        g.dirty_board = true;
    }
}

static void move_cursor(uint32_t keys)
{
    g.dirty_board = true;

    if (!g.cursor_visible)
    {
        g.cursor_visible = true;
        if (g.cursor == SQ_NONE)
            g.cursor = g.selected != SQ_NONE ? g.selected
                                             : SQ_MAKE(4, g.my_color == 'w' ? 1 : 6);
        return;
    }

    int file = SQ_FILE(g.cursor);
    int rank = SQ_RANK(g.cursor);
    int dir = g.flipped ? -1 : 1;

    if (keys & KEY_UP)
        rank += dir;
    if (keys & KEY_DOWN)
        rank -= dir;
    if (keys & KEY_RIGHT)
        file += dir;
    if (keys & KEY_LEFT)
        file -= dir;

    if (file < 0) file = 0;
    if (file > 7) file = 7;
    if (rank < 0) rank = 0;
    if (rank > 7) rank = 7;
    g.cursor = SQ_MAKE(file, rank);
}

static void panel_press(int slot)
{
    if (slot < 0 || slot >= BUTTON_COUNT)
        return;

    Button buttons[BUTTON_COUNT];
    panel_buttons(buttons);
    Button button = buttons[slot];

    // Everything except flipping needs the Pi.
    if (button != BTN_FLIP && button != BTN_MENU && !app_online())
        return;

    switch (button)
    {
        case BTN_RESIGN:
        case BTN_ABORT:
            if (g.confirm == button)
            {
                app_send(button == BTN_RESIGN ? "RESIGN" : "ABORT");
                g.confirm = BTN_NONE;
            }
            else
            {
                g.confirm = button;
                g.confirm_until = frame_count + CONFIRM_FRAMES;
            }
            break;
        case BTN_DRAW:
        case BTN_ACCEPT_DRAW:
            app_send("DRAW yes");
            break;
        case BTN_DECLINE_DRAW:
            app_send("DRAW no");
            break;
        case BTN_CLAIM:
            app_send("CLAIM");
            break;
        case BTN_FLIP:
            g.flipped = !g.flipped;
            g.dirty_board = true;
            break;
        case BTN_MENU:
            app_send("LEAVE");
            game_reset();
            app_set_screen(SCREEN_LOBBY);
            return;
        default:
            break;
    }

    g.dirty_panel = true;
}

void game_enter(void)
{
    mark_dirty();
}

void game_refresh_top(void)
{
    g.dirty_top = true;
}

void game_update(uint32_t keys, const Touch *touch)
{
    if (!g.loaded)
        return;

    if (g.confirm != BTN_NONE && (int32_t)(frame_count - g.confirm_until) > 0)
    {
        g.confirm = BTN_NONE;
        g.dirty_panel = true;
    }

    int gone = gone_remaining();
    if (gone != g.gone_shown)
    {
        g.gone_shown = gone;
        g.dirty_panel = true;
        g.dirty_top = true;
    }

    // A new message shows in the status line for a few seconds.
    bool message = message_active();
    if (app_log_version() != g.log_version || message != g.message_shown)
    {
        g.log_version = app_log_version();
        g.message_shown = message;
        g.dirty_top = true;
    }

    if (g.promo_open)
    {
        promotion_input(keys, touch);
    }
    else
    {
        if (touch->down)
        {
            if (touch->x < BOARD_PX)
            {
                if (g.cursor_visible)
                {
                    g.cursor_visible = false;
                    g.dirty_board = true;
                }
                board_click(gfx_square_at(touch->x, touch->y, g.flipped));
            }
            else
            {
                panel_press(touch->y / BUTTON_H);
            }
        }

        if (keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))
            move_cursor(keys);
        if ((keys & KEY_A) && g.cursor_visible)
            board_click(g.cursor);
        if ((keys & KEY_B) && g.selected != SQ_NONE)
        {
            g.selected = SQ_NONE;
            g.dirty_board = true;
        }
        if (keys & KEY_X)
        {
            g.flipped = !g.flipped;
            g.dirty_board = true;
        }
    }

    if (!g.loaded) // "Main menu" was pressed
        return;

    if (g.dirty_board)
    {
        draw_board();
        g.dirty_board = false;
    }
    if (g.dirty_panel)
    {
        draw_panel();
        g.dirty_panel = false;
    }
    if (g.dirty_top)
    {
        draw_top();
        g.dirty_top = false;
    }
    else if (frame_count % 6 == 0)
    {
        draw_clocks();
    }
}
