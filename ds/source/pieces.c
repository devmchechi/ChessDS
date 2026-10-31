#include <ctype.h>
#include <string.h>

#include "pieces.h"

// Piece silhouettes. 'X' is the body, '+' is a detail line inside the body and
// '.' is empty. pieces_init() draws a one-pixel outline around the body.
// Preview changes with: python ds/tools/art.py preview preview.png
static const char *const piece_art[PIECE_KINDS][PIECE_SIZE] = {
    { // Pawn
        "........................",
        "........................",
        "........................",
        "........................",
        "........................",
        "..........XXXX..........",
        ".........XXXXXX.........",
        ".........XXXXXX.........",
        ".........XXXXXX.........",
        "..........XXXX..........",
        "........XXXXXXXX........",
        "..........XXXX..........",
        "..........XXXX..........",
        ".........XXXXXX.........",
        "........XXXXXXXX........",
        ".......XXXXXXXXXX.......",
        "......XXXXXXXXXXXX......",
        "......XXXXXXXXXXXX......",
        ".....XXXXXXXXXXXXXX.....",
        ".....XXXXXXXXXXXXXX.....",
        ".....XXXXXXXXXXXXXX.....",
        "........................",
        "........................",
        "........................",
    },
    { // Knight
        "........................",
        "........................",
        "...........X.X..........",
        "..........XXXXX.........",
        ".........XXXXXXXX.......",
        "........XXXXXXXXXXX.....",
        ".......XXX+XXXXXXXXX....",
        "......XXXXXXXXXXXXXXX...",
        ".....XXXXXXXXXXXXXXXXX..",
        "....XXXXXXXXXXXXXXXXXX..",
        "...XXXXXXXXXXXXXXXXXXX..",
        "...XXXXXX.XXXXXXXXXXXX..",
        "...XXXX....XXXXXXXXXXX..",
        "....XX....XXXXXXXXXXXX..",
        ".........XXXXXXXXXXXXX..",
        "........XXXXXXXXXXXXXX..",
        ".......XXXXXXXXXXXXXX...",
        "......XXXXXXXXXXXXXXX...",
        ".....++++++++++++++++...",
        ".....XXXXXXXXXXXXXXXX...",
        ".....XXXXXXXXXXXXXXXX...",
        "........................",
        "........................",
        "........................",
    },
    { // Bishop
        "........................",
        "...........XX...........",
        "..........XXXX..........",
        "...........XX...........",
        "..........XXXX..........",
        ".........XXXXXX.........",
        "........XXXXX+XX........",
        ".......XXXXX+XXXX.......",
        ".......XXXX+XXXXX.......",
        ".......XXXXXXXXXX.......",
        "........XXXXXXXX........",
        ".........XXXXXX.........",
        "........++++++++........",
        ".........XXXXXX.........",
        ".........XXXXXX.........",
        "........XXXXXXXX........",
        ".......XXXXXXXXXX.......",
        "......++++++++++++......",
        ".....XXXXXXXXXXXXXX.....",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        "........................",
        "........................",
        "........................",
    },
    { // Rook
        "........................",
        "........................",
        "........................",
        "....XXX..XXXXXX..XXX....",
        "....XXX..XXXXXX..XXX....",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        ".....++++++++++++++.....",
        "......XXXXXXXXXXXX......",
        "......XXXXXXXXXXXX......",
        "......XXXXXXXXXXXX......",
        "......XXXXXXXXXXXX......",
        "......XXXXXXXXXXXX......",
        "......XXXXXXXXXXXX......",
        "......XXXXXXXXXXXX......",
        "......XXXXXXXXXXXX......",
        ".....++++++++++++++.....",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        "...XXXXXXXXXXXXXXXXXX...",
        "...XXXXXXXXXXXXXXXXXX...",
        "........................",
        "........................",
        "........................",
    },
    { // Queen
        "........................",
        "...........XX...........",
        "..XX.......XX.......XX..",
        "..XX..XX...XX...XX..XX..",
        "...X..XX...XX...XX..X...",
        "...XX.XXX.XXXX.XXX.XX...",
        "...XXXXXXXXXXXXXXXXXX...",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        ".....XXXXXXXXXXXXXX.....",
        ".....XXXXXXXXXXXXXX.....",
        "......XXXXXXXXXXXX......",
        "......++++++++++++......",
        "......XXXXXXXXXXXX......",
        ".......XXXXXXXXXX.......",
        ".......XXXXXXXXXX.......",
        "......XXXXXXXXXXXX......",
        ".....++++++++++++++.....",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        "........................",
        "........................",
        "........................",
    },
    { // King
        "........................",
        "...........XX...........",
        ".........XXXXXX.........",
        "...........XX...........",
        "..........XXXX..........",
        "..XXXXX..XXXXXX..XXXXX..",
        ".XXXXXXXXXXXXXXXXXXXXXX.",
        ".XXXXXXXXXX++XXXXXXXXXX.",
        ".XXXXXXXXXX++XXXXXXXXXX.",
        ".XXXXXXXXXX++XXXXXXXXXX.",
        "..XXXXXXXXX++XXXXXXXXX..",
        "..XXXXXXXXX++XXXXXXXXX..",
        "...XXXXXXXXXXXXXXXXXX...",
        "....XXXXXXXXXXXXXXXX....",
        "....++++++++++++++++....",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        "....++++++++++++++++....",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        "....XXXXXXXXXXXXXXXX....",
        "........................",
        "........................",
        "........................",
    },
};

uint8_t piece_mask[PIECE_KINDS][PIECE_SIZE][PIECE_SIZE];

int piece_kind(char fen_letter)
{
    static const char kinds[] = "pnbrqk";

    if (fen_letter == '\0')
        return -1;
    const char *found = strchr(kinds, tolower((unsigned char)fen_letter));
    return found != NULL ? (int)(found - kinds) : -1;
}

void pieces_init(void)
{
    for (int k = 0; k < PIECE_KINDS; k++)
    {
        uint8_t (*mask)[PIECE_SIZE] = piece_mask[k];

        for (int y = 0; y < PIECE_SIZE; y++)
        {
            for (int x = 0; x < PIECE_SIZE; x++)
            {
                char c = piece_art[k][y][x];
                mask[y][x] = c == 'X' ? PX_FILL : c == '+' ? PX_DETAIL : PX_EMPTY;
            }
        }

        // Outline: empty pixels next to the body (including diagonally).
        for (int y = 0; y < PIECE_SIZE; y++)
        {
            for (int x = 0; x < PIECE_SIZE; x++)
            {
                if (mask[y][x] != PX_EMPTY)
                    continue;

                for (int dy = -1; dy <= 1 && mask[y][x] == PX_EMPTY; dy++)
                {
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        int ny = y + dy, nx = x + dx;
                        if (ny < 0 || nx < 0 || ny >= PIECE_SIZE || nx >= PIECE_SIZE)
                            continue;
                        if (mask[ny][nx] >= PX_FILL)
                        {
                            mask[y][x] = PX_OUTLINE;
                            break;
                        }
                    }
                }
            }
        }
    }
}
