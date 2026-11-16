/* game_int.h - port: what game.c and game_rooms.c share (the original's
   statics in game.c, now across two files) */
#ifndef GAME_INT_H
#define GAME_INT_H
#include "game.h"
WORD rng(void);
WORD rng_range(WORD lo, WORD hi);
Item *spawn_item(GameState *gs, WORD type, WORD x, WORD y, WORD vx, WORD vy, WORD room);
Guest *spawn_guest(GameState *gs, WORD room, WORD x, WORD y);
void spawn_smoke(GameState *gs, WORD x, WORD y);
void update_amsterdam(GameState *gs, InputState *inp);
void update_tokyo(GameState *gs, InputState *inp);
void update_bayarea(GameState *gs, InputState *inp);
#endif
