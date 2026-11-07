/*
 * world.c - where Orb Hunter's world is kept on the Next (port: the
 * original has world_rooms[6][8][16][20] in memory, 15K): the world as
 * levels_init() makes it (gfx/world.bin, from the original's levels.c by
 * tools/gen_world.c) is in bank WORLD_BANK; a game plays on a copy in
 * bank WORK_BANK (doors it opens stay open until the next game); the
 * current room is copied into room[][] when it is entered.
 */
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "game.h"
#include "banks.h"

UBYTE room[ROOM_H][ROOM_W];
static uint16_t room_off;               /* the current room in the bank */

extern const ItemSpawn items_start[];
extern const WORD items_count;
ItemSpawn g_items[MAX_ITEMS];
WORD g_num_items;

void items_init(void)
{
    memcpy(g_items, items_start, items_count * sizeof(ItemSpawn));
    g_num_items = items_count;
}

void world_reset(void)
{
    dma_copy_banks(WORK_BANK, WORLD_BANK, 1);
}

/* the work bank in MMU slots 0 and 1 (0x0000-0x3FFF), then the ROM back */
static void work_in(void)
{
    ZXN_WRITE_MMU0(WORK_BANK * 2);
    ZXN_WRITE_MMU1(WORK_BANK * 2 + 1);
}

static void work_out(void)
{
    ZXN_WRITE_MMU0(0xFF);
    ZXN_WRITE_MMU1(0xFF);
}

void world_load_room(WORD rx, WORD ry)
{
    room_off = (uint16_t)(ry * WORLD_W + rx) * (ROOM_W * ROOM_H);
    work_in();
    memcpy(room, (const uint8_t *)room_off, sizeof(room));
    work_out();
}

void world_set_tile(WORD tx, WORD ty, UBYTE t)
{
    room[ty][tx] = t;
    work_in();
    *((uint8_t *)room_off + ty * ROOM_W + tx) = t;
    work_out();
}
