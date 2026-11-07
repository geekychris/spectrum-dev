/* game.c: Harbour Crane's game shell (the original's game.c and crane.h) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "hc.h"

enum { CS_TITLE, CS_PLAY, CS_OVER, CS_ENTRY };

typedef struct {
    uint8_t state, start_level, autoplay, paused, help, title_page;
    int16_t state_timer;
    char flash[24];
    int16_t flash_timer;
    uint8_t flash_ink;
    uint8_t splash_timer;
    int16_t splash_x;
} Crane;

extern Crane cr;
extern HCGame game;
extern uint8_t hc_probe[25];
void game_init(void);
void game_frame(uint8_t frames);         /* one pass of the main loop, frames since the last */
#endif
