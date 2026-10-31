#include <string.h>

#include "font.h"
#include "gfx.h"

static const Glyph *find_glyph(const Font *font, unsigned char c, const Font **owner)
{
    if (c >= 1 && c <= 6)
        font = &font_figurines;
    if (c < font->first || c >= font->first + font->count)
        c = '?';
    *owner = font;
    return &font->glyphs[c - font->first];
}

static void draw_glyph(const Font *font, const Glyph *glyph, int x, int y, uint16_t color)
{
    uint16_t *canvas = gfx_canvas();
    const uint8_t *data = font->data + glyph->offset;
    int row_bytes = (glyph->width + 1) / 2;

    for (int j = 0; j < font->height; j++, data += row_bytes)
    {
        int py = y + j;
        if (py < 0 || py >= SCREEN_H)
            continue;
        uint16_t *row = canvas + py * SCREEN_W;

        for (int i = 0; i < glyph->width; i++)
        {
            int px = x + i;
            if (px < 0 || px >= SCREEN_W)
                continue;
            int coverage = (data[i / 2] >> ((i & 1) * 4)) & 15;
            if (coverage != 0)
                row[px] = gfx_mix(row[px], color, coverage + (coverage >> 3));
        }
    }
}

int font_draw(const Font *font, int x, int y, uint16_t color, const char *text)
{
    for (const unsigned char *c = (const unsigned char *)text; *c != '\0'; c++)
    {
        const Font *owner;
        const Glyph *glyph = find_glyph(font, *c, &owner);
        // Figurines share the baseline of the text around them.
        int gy = y + font->ascent - owner->ascent;
        draw_glyph(owner, glyph, x + glyph->left, gy, color);
        x += glyph->advance;
    }
    gfx_touch();
    return x;
}

int font_width(const Font *font, const char *text)
{
    int width = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c != '\0'; c++)
    {
        const Font *owner;
        width += find_glyph(font, *c, &owner)->advance;
    }
    return width;
}

void font_draw_box(const Font *font, int x, int y, int w, int h, Align align, uint16_t color,
                   const char *text)
{
    // Room for the text plus "..." and the terminator.
    char fitted[96];
    size_t len = strlen(text);
    if (len > sizeof(fitted) - 4)
        len = sizeof(fitted) - 4;
    memcpy(fitted, text, len);
    fitted[len] = '\0';

    // Shorten with "..." until it fits.
    while (len > 0 && font_width(font, fitted) > w)
    {
        len--;
        while (len > 0 && fitted[len - 1] == ' ')
            len--;
        strcpy(fitted + len, "...");
    }

    int width = font_width(font, fitted);
    int tx = x;
    if (align == ALIGN_CENTER)
        tx = x + (w - width) / 2;
    else if (align == ALIGN_RIGHT)
        tx = x + w - width;

    int ty = y + (h - font->cap_height + 1) / 2 - font->cap_top;
    font_draw(font, tx, ty, color, fitted);
}
