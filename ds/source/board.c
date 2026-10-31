#include <ctype.h>
#include <string.h>

#include "board.h"

bool board_from_placement(Board *board, const char *placement)
{
    memset(board->piece, 0, sizeof(board->piece));

    int rank = 7;
    int file = 0;

    for (const char *s = placement; *s != '\0' && *s != ' '; s++)
    {
        char c = *s;

        if (c == '/')
        {
            if (--rank < 0)
                return false;
            file = 0;
        }
        else if (c >= '1' && c <= '8')
        {
            file += c - '0';
        }
        else if (strchr("pnbrqkPNBRQK", c) != NULL)
        {
            if (file > 7)
                return false;
            board->piece[SQ_MAKE(file, rank)] = c;
            file++;
        }
        else
        {
            return false;
        }
    }

    return rank == 0;
}

static int parse_square(const char *s)
{
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8')
        return SQ_NONE;
    return SQ_MAKE(s[0] - 'a', s[1] - '1');
}

bool move_parse(Move *move, const char *uci)
{
    size_t len = strlen(uci);
    if (len != 4 && len != 5)
        return false;

    int from = parse_square(uci);
    int to = parse_square(uci + 2);
    char promo = len == 5 ? uci[4] : 0;

    if (from == SQ_NONE || to == SQ_NONE)
        return false;
    if (promo != 0 && strchr("qrbn", promo) == NULL)
        return false;

    move->from = from;
    move->to = to;
    move->promo = promo;
    return true;
}

void move_format(const Move *move, char *out)
{
    out[0] = 'a' + SQ_FILE(move->from);
    out[1] = '1' + SQ_RANK(move->from);
    out[2] = 'a' + SQ_FILE(move->to);
    out[3] = '1' + SQ_RANK(move->to);
    out[4] = move->promo;
    out[5] = '\0';
}

void board_apply(Board *board, const Move *move)
{
    char piece = board->piece[move->from];
    if (piece == 0)
        return;

    bool white = isupper((unsigned char)piece);
    char kind = tolower((unsigned char)piece);
    int file_delta = SQ_FILE(move->to) - SQ_FILE(move->from);

    // Castling: the king moves two files, bring the rook along.
    if (kind == 'k' && (file_delta == 2 || file_delta == -2))
    {
        int rank = SQ_RANK(move->from);
        int rook_from = SQ_MAKE(file_delta > 0 ? 7 : 0, rank);
        int rook_to = SQ_MAKE(file_delta > 0 ? 5 : 3, rank);
        board->piece[rook_to] = board->piece[rook_from];
        board->piece[rook_from] = 0;
    }

    // En passant: a pawn moves diagonally to an empty square.
    if (kind == 'p' && file_delta != 0 && board->piece[move->to] == 0)
        board->piece[SQ_MAKE(SQ_FILE(move->to), SQ_RANK(move->from))] = 0;

    if (move->promo != 0)
        piece = white ? toupper((unsigned char)move->promo) : move->promo;

    board->piece[move->to] = piece;
    board->piece[move->from] = 0;
}

int board_find_king(const Board *board, bool white)
{
    char king = white ? 'K' : 'k';
    for (int sq = 0; sq < 64; sq++)
    {
        if (board->piece[sq] == king)
            return sq;
    }
    return SQ_NONE;
}
