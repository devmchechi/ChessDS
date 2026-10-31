// Lobby screen: menus to start games, waiting for an opponent, and incoming
// challenges.

#ifndef LOBBY_H
#define LOBBY_H

#include <stdbool.h>
#include <stdint.h>

#include "app.h"

void lobby_reset(void);
void lobby_enter(void);
void lobby_game_started(void);

// Handles FRIENDS, SEEKING, IDLE, CHALLENGED and CHALLENGE_GONE.
bool lobby_handle_line(const char *cmd, char *args);

void lobby_update(uint32_t keys, const Touch *touch);

#endif
