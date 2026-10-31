#include <ctype.h>
#include <string.h>

#include "gfx.h"
#include "pieces.h"

#define C_LIGHT       COLOR(29, 26, 22)
#define C_DARK        COLOR(22, 17, 12)
#define C_LAST_LIGHT  COLOR(25, 26, 13)
#define C_LAST_DARK   COLOR(21, 20, 7)
#define C_SEL_LIGHT   COLOR(17, 25, 15)
#define C_SEL_DARK    COLOR(12, 20, 10)
#define C_CHECK       COLOR(29, 9, 7)
#define C_TARGET      COLOR(8, 12, 7)

#define PIXELS (SCREEN_W * SCREEN_H)

// Indexed by [is_black][PX_*].
static const uint16_t piece_colors[2][4] = {
    { 0, COLOR(2, 2, 2), COLOR(31, 31, 30), COLOR(2, 2, 2) },
    { 0, COLOR(1, 1, 1), COLOR(6, 6, 7), COLOR(24, 24, 24) },
};

static uint16_t canvas[2][PIXELS] __attribute__((aligned(32)));
// One texture for both screens, as if they were cut from the same board.
static uint16_t wood[2][PIXELS] __attribute__((aligned(32)));
static uint16_t *vram[2];
static bool changed[2];
static GfxScreen target;
static uint16_t *fb;

// Wood texture: value noise stretched horizontally into grain.

static int hash(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) & 255;
}

static int smooth(int t)
{
    return (t * t * (768 - 2 * t)) >> 16;
}

// Smooth noise in 0-255 at 8.8 fixed-point coordinates.
static int noise(int fx, int fy)
{
    int x0 = fx >> 8, y0 = fy >> 8;
    int tx = smooth(fx & 255), ty = smooth(fy & 255);
    int a = hash(x0, y0), b = hash(x0 + 1, y0);
    int c = hash(x0, y0 + 1), d = hash(x0 + 1, y0 + 1);
    int ab = a + (((b - a) * tx) >> 8);
    int cd = c + (((d - c) * tx) >> 8);
    return ab + (((cd - ab) * ty) >> 8);
}

static uint16_t wood_pixel(int x, int y)
{
    static const int dark[3] = { 34, 22, 15 };
    static const int light[3] = { 88, 60, 40 };

    int warp = noise(x * 3, y * 6 + (40 << 8)) - 128; // lets the grain wave a little
    int yy = y * 256 + warp * 6;
    int grain = noise(x * 6, yy / 3);                  // long streaks
    int fine = noise(x * 48, yy);                      // thin fibers
    int broad = noise(x * 2 + (500 << 8), y * 20);     // slow light and dark areas

    int v = (grain * 6 + fine * 2 + broad * 4) / 12;
    v = (v - 60) * 255 / 150;
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;

    int rgb[3];
    for (int i = 0; i < 3; i++)
        rgb[i] = dark[i] + (light[i] - dark[i]) * v / 255;
    return COLOR8(rgb[0], rgb[1], rgb[2]);
}

void gfx_init(uint16_t *top_vram, uint16_t *bottom_vram)
{
    vram[GFX_TOP] = top_vram;
    vram[GFX_BOTTOM] = bottom_vram;

    for (int s = 0; s < 2; s++)
    {
        for (int y = 0; y < SCREEN_H; y++)
        {
            for (int x = 0; x < SCREEN_W; x++)
                wood[s][y * SCREEN_W + x] = wood_pixel(x, y + s * SCREEN_H);
        }
        memcpy(canvas[s], wood[s], sizeof(canvas[s]));
        changed[s] = true;
    }

    gfx_target(GFX_BOTTOM);
}

void gfx_target(GfxScreen screen)
{
    target = screen;
    fb = canvas[screen];
}

uint16_t *gfx_canvas(void)
{
    return fb;
}

void gfx_touch(void)
{
    changed[target] = true;
}

void gfx_flush(void)
{
    for (int s = 0; s < 2; s++)
    {
        if (!changed[s])
            continue;
        changed[s] = false;

        // 32-bit copy: VRAM can't be written one byte at a time.
        const uint32_t *src = (const uint32_t *)canvas[s];
        volatile uint32_t *dst = (volatile uint32_t *)vram[s];
        for (int i = 0; i < PIXELS / 2; i++)
            dst[i] = src[i];
    }
}

static bool clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > SCREEN_W)
        *w = SCREEN_W - *x;
    if (*y + *h > SCREEN_H)
        *h = SCREEN_H - *y;
    return *w > 0 && *h > 0;
}

void gfx_wood(int x, int y, int w, int h)
{
    if (!clip(&x, &y, &w, &h))
        return;
    for (int j = y; j < y + h; j++)
        memcpy(fb + j * SCREEN_W + x, wood[target] + j * SCREEN_W + x, w * 2);
    gfx_touch();
}

void gfx_fill(int x, int y, int w, int h, uint16_t color)
{
    if (!clip(&x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++)
    {
        uint16_t *row = fb + (y + j) * SCREEN_W + x;
        for (int i = 0; i < w; i++)
            row[i] = color;
    }
    gfx_touch();
}

void gfx_frame(int x, int y, int w, int h, int thickness, uint16_t color)
{
    gfx_fill(x, y, w, thickness, color);
    gfx_fill(x, y + h - thickness, w, thickness, color);
    gfx_fill(x, y, thickness, h, color);
    gfx_fill(x + w - thickness, y, thickness, h, color);
}

void gfx_round(int x, int y, int w, int h, uint16_t color, int alpha)
{
    // Pixels left out at each end of the first and last two rows.
    static const int corner[2] = { 2, 1 };

    for (int j = 0; j < h; j++)
    {
        int py = y + j;
        if (py < 0 || py >= SCREEN_H)
            continue;
        int inset = j < 2 ? corner[j] : h - 1 - j < 2 ? corner[h - 1 - j] : 0;
        int x0 = x + inset, x1 = x + w - inset;
        if (x0 < 0)
            x0 = 0;
        if (x1 > SCREEN_W)
            x1 = SCREEN_W;

        uint16_t *row = fb + py * SCREEN_W;
        if (alpha >= 16)
        {
            for (int px = x0; px < x1; px++)
                row[px] = color;
        }
        else
        {
            for (int px = x0; px < x1; px++)
                row[px] = gfx_mix(row[px], color, alpha);
        }
    }
    gfx_touch();
}

void gfx_dot(int cx, int cy, int radius, uint16_t color)
{
    for (int dy = -radius; dy <= radius; dy++)
    {
        for (int dx = -radius; dx <= radius; dx++)
        {
            if (dx * dx + dy * dy <= radius * radius + radius)
                gfx_fill(cx + dx, cy + dy, 1, 1, color);
        }
    }
}

void gfx_piece(int x, int y, char fen_letter)
{
    int kind = piece_kind(fen_letter);
    if (kind < 0)
        return;

    const uint16_t *colors = piece_colors[islower((unsigned char)fen_letter) ? 1 : 0];

    for (int j = 0; j < PIECE_SIZE; j++)
    {
        uint16_t *row = fb + (y + j) * SCREEN_W + x;
        for (int i = 0; i < PIECE_SIZE; i++)
        {
            uint8_t px = piece_mask[kind][j][i];
            if (px != PX_EMPTY)
                row[i] = colors[px];
        }
    }
    gfx_touch();
}

// A dot on empty target squares, corner triangles on captures.
static void draw_target(int x, int y, bool capture)
{
    for (int j = 0; j < SQ_PX; j++)
    {
        uint16_t *row = fb + (y + j) * SCREEN_W + x;
        for (int i = 0; i < SQ_PX; i++)
        {
            bool on;
            if (capture)
            {
                int dx = i < SQ_PX / 2 ? i : SQ_PX - 1 - i;
                int dy = j < SQ_PX / 2 ? j : SQ_PX - 1 - j;
                on = dx + dy < 6;
            }
            else
            {
                int dx = 2 * i - (SQ_PX - 1);
                int dy = 2 * j - (SQ_PX - 1);
                on = dx * dx + dy * dy <= 4 * 16;
            }
            if (on)
                row[i] = C_TARGET;
        }
    }
}

void gfx_square_origin(int sq, bool flipped, int *x, int *y)
{
    int file = SQ_FILE(sq);
    int rank = SQ_RANK(sq);
    *x = (flipped ? 7 - file : file) * SQ_PX;
    *y = (flipped ? rank : 7 - rank) * SQ_PX;
}

int gfx_square_at(int px, int py, bool flipped)
{
    if (px < 0 || py < 0 || px >= BOARD_PX || py >= BOARD_PX)
        return SQ_NONE;

    int col = px / SQ_PX;
    int row = py / SQ_PX;
    int file = flipped ? 7 - col : col;
    int rank = flipped ? row : 7 - row;
    return SQ_MAKE(file, rank);
}

void gfx_board(const BoardView *view)
{
    for (int sq = 0; sq < 64; sq++)
    {
        int x, y;
        gfx_square_origin(sq, view->flipped, &x, &y);

        bool light = ((SQ_FILE(sq) + SQ_RANK(sq)) & 1) != 0;
        uint16_t color = light ? C_LIGHT : C_DARK;
        if (sq == view->last_from || sq == view->last_to)
            color = light ? C_LAST_LIGHT : C_LAST_DARK;
        if (sq == view->selected)
            color = light ? C_SEL_LIGHT : C_SEL_DARK;
        if (sq == view->check_square)
            color = C_CHECK;
        gfx_fill(x, y, SQ_PX, SQ_PX, color);

        char piece = view->board->piece[sq];
        if (view->targets & (1ULL << sq))
            draw_target(x, y, piece != 0);
        if (piece != 0)
            gfx_piece(x, y, piece);
        if (sq == view->cursor)
            gfx_frame(x, y, SQ_PX, SQ_PX, 2, C_AMBER);
    }
}
