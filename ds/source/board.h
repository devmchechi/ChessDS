// Board representation used for drawing. The Pi is the source of truth for
// the rules: it sends the position and the list of legal moves.

#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>

// Squares are numbered a1 = 0, b1 = 1, ..., h8 = 63.
#define SQ_NONE           (-1)
#define SQ_FILE(sq)       ((sq) & 7)
#define SQ_RANK(sq)       ((sq) >> 3)
#define SQ_MAKE(file, rk) (((rk) << 3) | (file))

typedef struct {
    char piece[64]; // FEN letter ('P', 'n', ...) or 0 for an empty square
} Board;

typedef struct {
    signed char from;
    signed char to;
    char promo; // 'q', 'r', 'b', 'n', or 0
} Move;

// Parses the piece placement field of a FEN string ("rnbqkbnr/pppppppp/...").
bool board_from_placement(Board *board, const char *placement);

// Parses a UCI move such as "e2e4" or "e7e8q".
bool move_parse(Move *move, const char *uci);

// Writes the UCI form of a move into out (at least 6 bytes).
void move_format(const Move *move, char *out);

// Moves the pieces without checking legality (handles castling, en passant and
// promotion). Used to show our move immediately while the Pi confirms it.
void board_apply(Board *board, const Move *move);

// Square of the king of the given color, or SQ_NONE.
int board_find_king(const Board *board, bool white);

#endif
