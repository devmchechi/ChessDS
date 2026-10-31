// Drawing on both screens. Each screen is a 256x192 16-bit bitmap; drawing
// goes to a canvas in RAM that gfx_flush() copies to VRAM, so nothing is seen
// half drawn. This file has no libnds dependency so it also builds on a PC
// (see tools/preview).

#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
#include <stdint.h>

#include "board.h"

#define SCREEN_W 256
#define SCREEN_H 192
#define SQ_PX    24
#define BOARD_PX (SQ_PX * 8)
#define PANEL_X  BOARD_PX
#define PANEL_W  (SCREEN_W - BOARD_PX)

// 15-bit color with the "opaque" bit set, as required by bitmap backgrounds.
#define COLOR(r, g, b) ((uint16_t)(0x8000 | (r) | ((g) << 5) | ((b) << 10)))
#define COLOR8(r, g, b)  COLOR((r) >> 3, (g) >> 3, (b) >> 3)

#define C_BLACK       COLOR8(0, 0, 0)
#define C_WHITE       COLOR8(255, 255, 255)
#define C_TEXT        COLOR8(240, 232, 218) // cream
#define C_TEXT_DIM    COLOR8(184, 166, 146)
#define C_TEXT_MUTED  COLOR8(140, 122, 104)
#define C_TEXT_DARK   COLOR8(40, 30, 22)
#define C_GREEN       COLOR8(129, 182, 76)
#define C_GREEN_DARK  COLOR8(84, 124, 52)
#define C_RED         COLOR8(200, 72, 54)
#define C_RED_DARK    COLOR8(138, 44, 32)
#define C_AMBER       COLOR8(240, 190, 84)
#define C_CHAT        COLOR8(226, 204, 160)
#define C_CLOCK_ON    COLOR8(236, 228, 212)
#define C_AVATAR      COLOR8(222, 200, 166)

typedef enum {
    GFX_TOP,
    GFX_BOTTOM,
} GfxScreen;

typedef struct {
    const Board *board;
    bool flipped;      // black at the bottom
    int selected;      // highlighted square or SQ_NONE
    int cursor;        // D-pad cursor or SQ_NONE
    int last_from;     // last move, or SQ_NONE
    int last_to;
    int check_square;  // king in check, or SQ_NONE
    uint64_t targets;  // bit per square: legal destinations of the selection
} BoardView;

// Generates the wood texture (takes a moment) and clears both screens.
void gfx_init(uint16_t *top_vram, uint16_t *bottom_vram);
// Selects the screen the drawing functions draw on.
void gfx_target(GfxScreen screen);
// Copies the canvases that changed to VRAM. Call right after the VBlank.
void gfx_flush(void);

uint16_t *gfx_canvas(void); // canvas of the current target (for font.c)
void gfx_touch(void);       // marks the current target as changed

static inline uint16_t gfx_mix(uint16_t back, uint16_t front, int alpha) // alpha 0-16
{
    int r = back & 31, g = (back >> 5) & 31, b = (back >> 10) & 31;
    r += (((front & 31) - r) * alpha) >> 4;
    g += ((((front >> 5) & 31) - g) * alpha) >> 4;
    b += ((((front >> 10) & 31) - b) * alpha) >> 4;
    return 0x8000 | r | (g << 5) | (b << 10);
}

void gfx_wood(int x, int y, int w, int h);
void gfx_fill(int x, int y, int w, int h, uint16_t color);
void gfx_frame(int x, int y, int w, int h, int thickness, uint16_t color);
// Blends a rectangle with rounded corners over what is there (alpha 0-16).
void gfx_round(int x, int y, int w, int h, uint16_t color, int alpha);
void gfx_dot(int cx, int cy, int radius, uint16_t color);
void gfx_piece(int x, int y, char fen_letter);
void gfx_board(const BoardView *view);

// Square under a screen position, or SQ_NONE.
int gfx_square_at(int px, int py, bool flipped);
void gfx_square_origin(int sq, bool flipped, int *x, int *y);

#endif
