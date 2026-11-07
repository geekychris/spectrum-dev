/* game.c: Marble Tilt's game shell (the original's game.c and tilt.h) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "mt.h"

enum { TS_TITLE, TS_PLAY, TS_OVER, TS_ENTRY };

typedef struct {
    uint8_t state, start_level, autoplay, paused, help, title_page;
    int16_t state_timer;
    char flash[24];
    int16_t flash_timer;
    uint8_t flash_ink;
} Tilt;

extern Tilt tl;
extern MTGame game;
void game_init(void);
void game_frame(uint8_t frames);         /* one pass of the main loop, frames since the last */
#endif
