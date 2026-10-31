"""Preview the piece art from source/pieces.c and build the ROM icon.

    python ds/tools/art.py preview preview.png   # board with every piece, 4x size
    python ds/tools/art.py icon                  # writes ds/icon.gif

Needs Pillow (pip install pillow). The colors and the outline rule mirror
source/gfx.c and source/pieces.c.
"""

import re
import sys
from pathlib import Path

from PIL import Image

DS_DIR = Path(__file__).resolve().parent.parent
PIECES_C = DS_DIR / "source" / "pieces.c"
SIZE = 24
KINDS = "pnbrqk"

EMPTY, OUTLINE, FILL, DETAIL = range(4)


def rgb15(r, g, b):
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31)


LIGHT = rgb15(29, 26, 22)
DARK = rgb15(22, 17, 12)
PIECE_COLORS = {
    "white": {OUTLINE: rgb15(2, 2, 2), FILL: rgb15(31, 31, 30), DETAIL: rgb15(2, 2, 2)},
    "black": {OUTLINE: rgb15(1, 1, 1), FILL: rgb15(6, 6, 7), DETAIL: rgb15(24, 24, 24)},
}


def load_masks():
    rows = re.findall(r'^\s*"([^"]*)",\s*$', PIECES_C.read_text(), re.MULTILINE)
    if len(rows) != len(KINDS) * SIZE:
        sys.exit(f"expected {len(KINDS) * SIZE} art rows in pieces.c, found {len(rows)}")
    for number, row in enumerate(rows):
        if len(row) != SIZE or set(row) - set(".X+"):
            sys.exit(f"bad art row {number % SIZE} of {KINDS[number // SIZE]}: {row!r}")

    masks = {}
    for k, kind in enumerate(KINDS):
        art = rows[k * SIZE:(k + 1) * SIZE]
        mask = [[FILL if c == "X" else DETAIL if c == "+" else EMPTY for c in row] for row in art]
        for y in range(SIZE):
            for x in range(SIZE):
                if mask[y][x] != EMPTY:
                    continue
                if any(
                    0 <= y + dy < SIZE and 0 <= x + dx < SIZE and mask[y + dy][x + dx] >= FILL
                    for dy in (-1, 0, 1)
                    for dx in (-1, 0, 1)
                ):
                    mask[y][x] = OUTLINE
        masks[kind] = mask
    return masks


def draw_piece(image, masks, kind, color, ox, oy):
    colors = PIECE_COLORS[color]
    for y, row in enumerate(masks[kind]):
        for x, px in enumerate(row):
            if px != EMPTY:
                image.putpixel((ox + x, oy + y), colors[px])


def preview(path):
    masks = load_masks()
    board = Image.new("RGB", (SIZE * 6, SIZE * 4))
    for row in range(4):
        for col in range(6):
            fill = LIGHT if (row + col) % 2 == 0 else DARK
            board.paste(fill, (col * SIZE, row * SIZE, (col + 1) * SIZE, (row + 1) * SIZE))
    for col, kind in enumerate(KINDS):
        draw_piece(board, masks, kind, "white", col * SIZE, 0)
        draw_piece(board, masks, kind, "black", col * SIZE, SIZE)
        draw_piece(board, masks, kind, "white", col * SIZE, 2 * SIZE)
        draw_piece(board, masks, kind, "black", col * SIZE, 3 * SIZE)
    board.resize((board.width * 4, board.height * 4), Image.NEAREST).save(path)
    print(f"wrote {path}")


def icon():
    # 32x32, at most 16 colors: a white knight on a two-tone board.
    masks = load_masks()
    image = Image.new("RGB", (32, 32), DARK)
    image.paste(LIGHT, (0, 0, 16, 16))
    image.paste(LIGHT, (16, 16, 32, 32))
    draw_piece(image, masks, "n", "white", 4, 5)
    path = DS_DIR / "icon.gif"
    image.quantize(colors=16).save(path)
    print(f"wrote {path}")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "icon":
        icon()
    elif len(sys.argv) >= 3 and sys.argv[1] == "preview":
        preview(sys.argv[2])
    else:
        sys.exit(__doc__)
