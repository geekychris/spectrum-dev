/* view.c: the board on Layer 2 and the marble as a sprite */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "mt.h"

void view_init(void);
/* the tilt step drawn for a tilt (-VIEW_TILTS..VIEW_TILTS) */
int8_t view_step(fix tilt);
/* the board the game wants shown (level, gates, tilt steps): a board
   drawn before is shown at the next view_flip, a new one is drawn by
   view_work, a frame's worth at a time */
void view_update(const MTGame *g);
void view_work(void);                    /* draw until the next frame interrupt */
uint8_t view_flip(void);                 /* after frame_wait: show a board that is ready */
extern uint8_t view_slots, view_roll, view_pitch;
extern uint16_t view_hits;
/* the marble and its shadow (sprites 0 and 1), masked by what is in front */
void view_marble(const MTGame *g);
void view_hide_marble(void);
extern uint16_t view_draws;
extern uint32_t view_lines;              /* boards drawn (the test reads it) */
#endif
