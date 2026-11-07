/*
 * state_hash.h - a scripted game and a hash of the game's state, the same
 * on the host (tools/host_trace.c, the original's code compiled for the Mac)
 * and the Next (main.c, the "hash_req" run): if the hashes agree, the Next
 * computes exactly what the original computes.
 */
#ifndef STATE_HASH_H
#define STATE_HASH_H
#include <stdint.h>
#include "game.h"

#define SCRIPT_FRAMES 3000

/* the input of frame f: bit 0 left, 1 right, 2 jump, 3 down, 4 fire */
uint8_t script_input(uint16_t f);
uint32_t state_hash(const GameState *gs);
/* a new game (game_init, port_fixes), then the scripted frames until the
   game is over or SCRIPT_FRAMES; returns the state's hash, *frames the
   frames played */
uint32_t hash_game(GameState *gs, uint16_t *frames);

#endif
