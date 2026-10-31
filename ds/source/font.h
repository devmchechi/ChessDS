// Anti-aliased text drawn into the current gfx target. Font data is generated
// by ds/tools/fonts.py into font_data.c.

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

typedef struct {
    uint8_t width;   // bitmap width in pixels
    int8_t left;     // bitmap position relative to the pen
    uint8_t advance; // pen movement
    uint16_t offset; // into Font.data: rows of 4-bit coverage, 2 pixels per byte
} Glyph;

typedef struct {
    uint8_t height;     // line height = bitmap height of every glyph
    uint8_t ascent;     // baseline position from the top of the line
    uint8_t cap_top;    // top of capital letters and digits from the top of the line
    uint8_t cap_height;
    uint8_t first;      // character code of glyphs[0]
    uint8_t count;
    const Glyph *glyphs;
    const uint8_t *data;
} Font;

extern const Font font_regular;
extern const Font font_bold;
extern const Font font_large;
extern const Font font_figurines; // characters 1-6: pawn, knight, bishop, rook, queen, king

typedef enum {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT,
} Align;

// Draws text with the top of its line at y. Characters 1-6 draw figurines.
// Returns the x position after the text.
int font_draw(const Font *font, int x, int y, uint16_t color, const char *text);

int font_width(const Font *font, const char *text);

// Draws text inside the box [x, x + w) x [y, y + h): aligned horizontally,
// with capital letters centered vertically. Text that doesn't fit ends in "...".
void font_draw_box(const Font *font, int x, int y, int w, int h, Align align, uint16_t color,
                   const char *text);

#endif
