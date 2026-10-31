"""Generate ds/source/font_data.c: anti-aliased fonts and move-list figurines.

    python ds/tools/fonts.py

The text font is Aileron (public domain, CC0), which ships with Pillow as its
default font. Bold is made by drawing each glyph twice, one pixel apart. The
figurines (small piece icons used in the move list) are drawn below. Pixels are
stored as 4-bit coverage.
"""

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

DS_DIR = Path(__file__).resolve().parent.parent
OUTPUT = DS_DIR / "source" / "font_data.c"

CHARS = [chr(c) for c in range(32, 127)]
FONTS = [
    # name, size, bold
    ("font_regular", 11, False),
    ("font_bold", 11, True),
    ("font_large", 20, True),
]
FIGURINE_ASCENT = 11  # matches the size-11 fonts so figurines sit on the baseline
SPACE_WIDTH = 0.36    # Aileron's spaces are very narrow at small sizes; em fraction

# Move-list piece icons, 10 pixels tall, standing on the baseline.
# '#' is ink, '+' half ink, '.' empty.
FIGURINES = {
    "p": [
        "...##...",
        "..####..",
        "..####..",
        "...##...",
        "..####..",
        "...##...",
        "...##...",
        "..####..",
        ".######.",
        ".######.",
    ],
    "n": [
        "....+#+..",
        "...#####.",
        "..#######",
        ".##.#####",
        "#########",
        "#+.######",
        "...######",
        "..#######",
        ".########",
        ".########",
    ],
    "b": [
        "...##...",
        "..####..",
        ".###.##.",
        ".##.###.",
        ".######.",
        "..####..",
        "...##...",
        "..####..",
        ".######.",
        "########",
    ],
    "r": [
        "##.###.##",
        "##.###.##",
        "#########",
        ".#######.",
        "..#####..",
        "..#####..",
        "..#####..",
        ".#######.",
        "#########",
        "#########",
    ],
    "q": [
        "#...##...#",
        "##..##..##",
        "##.####.##",
        "##########",
        "##########",
        ".########.",
        "..######..",
        "..######..",
        ".########.",
        ".########.",
    ],
    "k": [
        "....##....",
        "...####...",
        "....##....",
        ".##.##.##.",
        "##########",
        "##########",
        ".########.",
        "..######..",
        "..######..",
        ".########.",
    ],
}


def coverage_to_alpha(value):
    # Slightly heavier than linear so small text stays readable on wood.
    return round(((value / 255) ** 0.8) * 15)


def glyph_from_image(image, origin_x):
    """Crop empty columns. Returns (left bearing, width, rows of 4-bit alpha)."""
    columns = [x for x in range(image.width) if any(image.getpixel((x, y)) for y in range(image.height))]
    if not columns:
        return 0, 0, []
    x0, x1 = columns[0], columns[-1] + 1
    rows = [[coverage_to_alpha(image.getpixel((x, y))) for x in range(x0, x1)]
            for y in range(image.height)]
    return x0 - origin_x, x1 - x0, rows


def render_font(size, bold):
    font = ImageFont.load_default(size=size)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    glyphs = []
    for ch in CHARS:
        advance = round(font.getlength(ch))
        if ch == " ":
            advance = max(advance, round(size * SPACE_WIDTH))
        image = Image.new("L", (advance + 12, height), 0)
        ImageDraw.Draw(image).text((4, 0), ch, font=font, fill=255)
        if bold:
            image = ImageChops.lighter(image, ImageChops.offset(image, 1, 0))
            advance += 1
        left, width, rows = glyph_from_image(image, 4)
        glyphs.append((left, width, advance, rows))
    _, top, _, bottom = font.getbbox("H")
    return {"height": height, "ascent": ascent, "cap_top": top, "cap_height": bottom - top,
            "first": 32, "glyphs": glyphs}


def render_figurines(height):
    """The FIGURINES as glyphs 1-6 (p n b r q k), bottom row just above the baseline."""
    glyphs = []
    for kind in "pnbrqk":
        art = FIGURINES[kind]
        top = FIGURINE_ASCENT - len(art)
        image = Image.new("L", (len(art[0]), height), 0)
        for y, row in enumerate(art):
            for x, c in enumerate(row):
                image.putpixel((x, top + y), {"#": 255, "+": 128}.get(c, 0))
        left, width, glyph_rows = glyph_from_image(image, 0)
        glyphs.append((left, width, width + 2, glyph_rows))
    return {"height": height, "ascent": FIGURINE_ASCENT, "cap_top": 0, "cap_height": 0,
            "first": 1, "glyphs": glyphs}


def emit_font(name, font, out):
    data = bytearray()
    entries = []
    for left, width, advance, rows in font["glyphs"]:
        entries.append((width, left, advance, len(data)))
        for row in rows:
            packed = row + [0] * (len(row) % 2)
            data.extend(packed[i] | packed[i + 1] << 4 for i in range(0, len(packed), 2))
    out.append(f"static const uint8_t {name}_data[{len(data)}] = {{")
    for i in range(0, len(data), 24):
        out.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    out.append("};")
    out.append(f"static const Glyph {name}_glyphs[{len(entries)}] = {{")
    for width, left, advance, offset in entries:
        out.append(f"    {{ {width}, {left}, {advance}, {offset} }},")
    out.append("};")
    out.append(
        f"const Font {name} = {{ {font['height']}, {font['ascent']}, {font['cap_top']}, "
        f"{font['cap_height']}, {font['first']}, {len(entries)}, {name}_glyphs, {name}_data }};"
    )
    out.append("")


def main():
    out = [
        "// Generated by ds/tools/fonts.py - do not edit.",
        "// Text: Aileron by Sora Sagano (dotcolon.net), public domain (CC0).",
        "",
        '#include "font.h"',
        "",
    ]
    fonts = {name: render_font(size, bold) for name, size, bold in FONTS}
    for name, font in fonts.items():
        emit_font(name, font, out)
    emit_font("font_figurines", render_figurines(fonts["font_bold"]["height"]), out)
    OUTPUT.write_text("\n".join(out))
    for name, font in fonts.items():
        print(f"{name}: height {font['height']}, ascent {font['ascent']}, "
              f"cap {font['cap_top']}+{font['cap_height']}")
    print(f"wrote {OUTPUT}")


if __name__ == "__main__":
    main()
