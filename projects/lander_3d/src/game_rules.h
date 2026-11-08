/* game_rules.c: the shell's work with the rules (bank 20: call with LOGIC()) */
#ifndef GAME_RULES_H
#define GAME_RULES_H
#include <stdint.h>
uint8_t rules_tick(uint8_t held);        /* 1: the demo's level is over, 2: a landing or crash */
void game_outcome(void);                 /* game_out.c (bank 24: call with HUD()) */
void rules_autopilot_reset(void);
void hud_values(void);
void selftest(void);
void set_state(uint8_t s);               /* game.c */
#endif
