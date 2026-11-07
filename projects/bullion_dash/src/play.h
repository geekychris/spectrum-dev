/* Bullion Dash - one 50 Hz frame of play (the original main.c's STATE_PLAYING) */
#ifndef PLAY_H
#define PLAY_H

#include <stdint.h>

#define PLAY_ON      0
#define PLAY_CAUGHT  1          /* a guard got the player */
#define PLAY_BURIED  2          /* port: a brick grew back on the player, or a fall off the grid */
#define PLAY_GAVE_UP 3          /* port: BREAK */
#define PLAY_WON     4          /* all the gold, and the top row */

/* held: IN_* bits held; quit: BREAK pressed */
uint8_t play_tick(uint8_t held, uint8_t quit);

#endif
