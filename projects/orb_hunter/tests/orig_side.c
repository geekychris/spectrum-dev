/* the original's rules (its game.c and levels.c, as they are), every name
   prefixed so they link beside the port's */
#define game_init o_game_init
#define game_load_room o_game_load_room
#define game_update o_game_update
#define game_spawn_particles o_game_spawn_particles
#define tile_props o_tile_props
#define world_rooms o_world_rooms
#define g_items o_g_items
#define g_num_items o_g_num_items
#define levels_init o_levels_init
#include ORIG_GAME
#include ORIG_LEVELS
#include "snap.h"
#define ROOM_TILE(y, x) world_rooms[gs.room_y][gs.room_x][y][x]
#include "side.h"

void orig_init(void) { game_init(&gs); }
void orig_update(int l, int r, int u, int d, int j, int f) { game_update(&gs, l, r, u, d, j, f); }
void orig_snap(Snap *s) { fill_snap(s); }
void orig_give_all(void) { give_all(); }
void orig_resume(void) { gs.state = STATE_PLAYING; }
