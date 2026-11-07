/* screens.c: the text screens (in bank 24, with hud.c) */
#ifndef SCREENS_H
#define SCREENS_H
#include <stdint.h>
#include "zxgame.h"
extern uint8_t hud_dirty;                /* everything is redrawn */
extern int16_t scr_err;                  /* the roll error (od_roll_error) */
extern hs_editor ed;
extern int8_t ed_place;
void screens_draw(void);                 /* with HUD() mapped */
#endif
