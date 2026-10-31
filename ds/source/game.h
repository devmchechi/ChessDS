// In-game screen: the board on the touch screen; players, clocks and moves on
// the top screen.

#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "app.h"

void game_reset(void);
bool game_loaded(void);

// Called when (re)connecting to the Pi. If the Pi doesn't send the game again
// before game_end_sync(), the game is dropped. Returns true if a game is loaded.
void game_begin_sync(void);
bool game_end_sync(void);

// Handles GAME, STATE, LEGAL, MOVES, END and GONE. Returns false for other lines.
bool game_handle_line(const char *cmd, char *args);

void game_enter(void);
void game_refresh_top(void);
void game_update(uint32_t keys, const Touch *touch);

#endif
