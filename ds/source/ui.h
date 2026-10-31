// Widgets shared by the screens, drawn on the current gfx target.

#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_NORMAL,
    UI_PRIMARY,  // green
    UI_DANGER,   // red
    UI_DISABLED,
} UiStyle;

// A darker area of the wood that groups content.
void ui_card(int x, int y, int w, int h);
// A button with one or two centered lines of text (line2 may be NULL).
void ui_button(int x, int y, int w, int h, UiStyle style, const char *line1, const char *line2);
// A menu entry.
void ui_item(int x, int y, int w, int h, bool selected, const char *text);
// A chess clock: dark when idle, light when running, red when low.
void ui_clock(int x, int y, int w, int h, const char *text, bool running, bool low);
// A 28x28 tile with a piece, like a player avatar.
void ui_avatar(int x, int y, char fen_letter);
// A small colored label whose right edge is at right_x.
void ui_pill(int right_x, int y, uint16_t color, const char *text);

#endif
