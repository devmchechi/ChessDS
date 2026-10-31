// Shared definitions for the ChessDS modules.

#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#define FPS       60
#define NAME_LEN  32
#define LOG_LINES 3

typedef enum {
    SCREEN_CONNECT, // joining WiFi / connecting to the Pi
    SCREEN_LOBBY,   // menus, seeking, incoming challenges
    SCREEN_GAME,    // board
} Screen;

typedef struct {
    bool down; // the stylus touched the screen this frame
    int x;
    int y;
} Touch;

typedef struct {
    char text[64];
    uint16_t color;
    uint32_t frame; // when it was logged
} LogLine;

extern uint32_t frame_count;

// True when connected to the Pi and logged in.
bool app_online(void);

// Sends one protocol line to the Pi (the newline is added).
void app_send(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// Adds a message for the player (shown on the top screen).
void app_log(uint16_t color, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// Message number `age` (0 is the newest), or NULL.
const LogLine *app_log_line(int age);
// Changes every time a message is added.
uint32_t app_log_version(void);

// Switches screens. The new screen is drawn at the end of the frame.
void app_set_screen(Screen screen);

// Returns the next space-separated token of *s and advances *s past it (and
// past any following spaces), modifying the string in place. Returns NULL
// when there are no tokens left.
char *next_token(char **s);

#endif
