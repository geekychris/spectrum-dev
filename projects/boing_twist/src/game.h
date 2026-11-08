/* game.c: Boing Twist's game shell (the original's main_os4.c) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "bt_core.h"
#include "zxgame.h"

enum { SC_TITLE, SC_SELECT, SC_PLAY, SC_SOLVED, SC_ENTRY };

extern BtPuzzle P;
extern uint8_t screen, busy, sel_band, sel_half, pitch, paused, peeking;
extern int16_t anim_move;
extern uint8_t help, title_page, gave_up, best_new, sel_level, sel_pic, sel_row, play_level, play_pic, hud_dirty;
extern uint16_t secs, peeks, score;
extern uint16_t best_mv[][4], best_s[][4];
extern hs_editor ed;
extern char tmp[44];
uint8_t pic_allowed(uint8_t i, uint8_t level);
void screens_draw(void);                 /* screens.c: the text for the screen */
void game_init(void);
void game_after_wait(void);              /* right after frame_wait: show what was drawn */
void game_frame(uint8_t frames);         /* one pass of the main loop, frames since the last */
void game_sample(void);                  /* from the frame interrupt (frame_hook) */
#endif
