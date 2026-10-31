#include <ctype.h>
#include <stddef.h>

#include "font.h"
#include "gfx.h"
#include "ui.h"

#define BEVEL 3 // darker strip under colored buttons

void ui_card(int x, int y, int w, int h)
{
    gfx_round(x, y, w, h, C_BLACK, 6);
}

// Background of buttons and menu entries. Returns the text color and the
// height available for the text.
static uint16_t button_back(int x, int y, int w, int h, UiStyle style, int *text_h)
{
    *text_h = h;
    switch (style)
    {
        case UI_PRIMARY:
        case UI_DANGER:
            gfx_round(x, y, w, h, style == UI_PRIMARY ? C_GREEN_DARK : C_RED_DARK, 16);
            gfx_round(x, y, w, h - BEVEL, style == UI_PRIMARY ? C_GREEN : C_RED, 16);
            *text_h = h - BEVEL;
            return C_WHITE;
        case UI_DISABLED:
            gfx_round(x, y, w, h, C_BLACK, 4);
            return C_TEXT_MUTED;
        default:
            gfx_round(x, y, w, h, C_BLACK, 8);
            gfx_round(x + 2, y, w - 4, 1, C_WHITE, 2); // faint top edge
            return C_TEXT;
    }
}

void ui_button(int x, int y, int w, int h, UiStyle style, const char *line1, const char *line2)
{
    int text_h;
    uint16_t color = button_back(x, y, w, h, style, &text_h);

    if (line2 == NULL || line2[0] == '\0')
    {
        font_draw_box(&font_bold, x + 2, y, w - 4, text_h, ALIGN_CENTER, color, line1);
        return;
    }

    // Two lines of capitals with a 5 pixel gap.
    int block = 2 * font_bold.cap_height + 5;
    int top = y + (text_h - block + 1) / 2;
    font_draw_box(&font_bold, x + 2, top, w - 4, font_bold.cap_height, ALIGN_CENTER, color, line1);
    font_draw_box(&font_bold, x + 2, top + font_bold.cap_height + 5, w - 4, font_bold.cap_height,
                  ALIGN_CENTER, color, line2);
}

void ui_item(int x, int y, int w, int h, bool selected, const char *text)
{
    int text_h;
    uint16_t color = button_back(x, y, w, h, selected ? UI_PRIMARY : UI_NORMAL, &text_h);
    font_draw_box(&font_bold, x + 12, y, w - 24, text_h, ALIGN_LEFT, color, text);
}

void ui_clock(int x, int y, int w, int h, const char *text, bool running, bool low)
{
    uint16_t back = C_BLACK, color = C_TEXT_DIM;
    int alpha = 8;
    if (running)
    {
        back = low ? C_RED : C_CLOCK_ON;
        color = low ? C_WHITE : C_TEXT_DARK;
        alpha = 16;
    }
    gfx_round(x, y, w, h, back, alpha);
    font_draw_box(&font_large, x + 6, y, w - 12, h, ALIGN_RIGHT, color, text);
}

void ui_avatar(int x, int y, char fen_letter)
{
    bool white = isupper((unsigned char)fen_letter);
    gfx_round(x, y, 28, 28, white ? C_AVATAR : COLOR8(150, 120, 88), 16);
    gfx_piece(x + 2, y + 2, fen_letter);
}

void ui_pill(int right_x, int y, uint16_t color, const char *text)
{
    int w = font_width(&font_bold, text) + 14;
    gfx_round(right_x - w, y, w, 16, color, 16);
    font_draw_box(&font_bold, right_x - w, y, w, 16, ALIGN_CENTER, C_WHITE, text);
}
