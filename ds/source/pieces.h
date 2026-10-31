// Chess piece bitmaps (24x24), built at startup from the ASCII art in pieces.c.

#ifndef PIECES_H
#define PIECES_H

#include <stdint.h>

#define PIECE_SIZE  24
#define PIECE_KINDS 6

enum {
    PX_EMPTY = 0,
    PX_OUTLINE,
    PX_FILL,
    PX_DETAIL,
};

// [kind][y][x], kind in the order of piece_kind().
extern uint8_t piece_mask[PIECE_KINDS][PIECE_SIZE][PIECE_SIZE];

void pieces_init(void);

// 0..5 for p, n, b, r, q, k (either case), -1 for anything else.
int piece_kind(char fen_letter);

#endif
