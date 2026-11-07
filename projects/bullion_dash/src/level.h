/* Bullion Dash - level management (the original's level.h) */
#ifndef LEVEL_H
#define LEVEL_H

#include <stdint.h>

/* the tile map's storage and the guards' pathfinding: once at start-up */
void level_init(void);
/* load a level by number (1-based) into the game state */
void level_load(int16_t level_num);
/* tick the brick regeneration timers */
void level_tick_bricks(void);
/* reveal hidden ladders (called when all gold is collected) */
void level_reveal_hidden_ladders(void);
/* dig a brick at grid position - returns 1 if successful */
uint8_t level_dig_brick(int16_t gx, int16_t gy);
/* 1 if the level has hidden ladders */
uint8_t level_count_hidden(void);
/* count remaining gold tiles */
int16_t level_count_gold(void);
/* turn TILE_PLAYER / TILE_ENEMY markers into the player and the enemies and
   clear them (the editor's test play) */
void level_activate_spawn_markers(void);
/* every cell's pathfinding flags from the tiles (after changing many) */
void level_refresh_paths(void);

#endif
