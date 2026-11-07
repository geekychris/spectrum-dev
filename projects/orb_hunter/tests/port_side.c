/* the port's rules (../src/game.c) with the world on the host (world_host) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "snap.h"
#define ROOM_TILE(y, x) room[y][x]
#include "side.h"

/* world.c for the host: the world from gfx/world.bin */
static UBYTE world[WORLD_H * WORLD_W * ROOM_H * ROOM_W], work[sizeof(world)];
static long room_off;
UBYTE room[ROOM_H][ROOM_W];
extern const ItemSpawn items_start[];
extern const WORD items_count;
ItemSpawn g_items[MAX_ITEMS];
WORD g_num_items;

void items_init(void) { memcpy(g_items, items_start, items_count * sizeof(ItemSpawn)); g_num_items = items_count; }
void world_reset(void) { memcpy(work, world, sizeof(work)); }
void world_load_room(WORD rx, WORD ry) { room_off = (long)(ry * WORLD_W + rx) * 320; memcpy(room, work + room_off, 320); }
void world_set_tile(WORD tx, WORD ty, UBYTE t) { room[ty][tx] = t; work[room_off + ty * ROOM_W + tx] = t; }

void port_init(void)
{
    static int loaded;
    if (!loaded) {
        FILE *f = fopen("../gfx/world.bin", "rb");
        if (!f || fread(world, 1, sizeof(world), f) != sizeof(world)) { printf("no gfx/world.bin\n"); exit(2); }
        fclose(f);
        loaded = 1;
    }
    game_init(&gs);
}
void port_update(int l, int r, int u, int d, int j, int f) { game_update(&gs, l, r, u, d, j, f); }
void port_snap(Snap *s) { fill_snap(s); }
void port_give_all(void) { give_all(); }

/* the effects: counted by host_diff (both sides call the same ones) */
int sfx_count;
void sfx_shoot(void) { sfx_count++; }
void sfx_hit(void) { sfx_count++; }
void sfx_explode(void) { sfx_count++; }
void sfx_powerup(void) { sfx_count++; }
void sfx_jump(void) { sfx_count++; }
void sfx_player_hit(void) { sfx_count++; }
void sfx_missile(void) { sfx_count++; }
void sfx_door(void) { sfx_count++; }
void port_resume(void) { extern void orig_resume(void); gs.state = STATE_PLAYING; orig_resume(); }
