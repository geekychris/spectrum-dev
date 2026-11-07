/*
 * world.c - where Orb Hunter's world is kept on the Next (port: the
 * original has world_rooms[6][8][16][20] in memory, 15K): the world as
 * levels_init() makes it (gfx/world.bin, from the original's levels.c by
 * tools/gen_world.c) is in bank WORLD_BANK; a game plays on a copy in
 * bank WORK_BANK (doors it opens stay open until the next game), through
 * MMU slots 2 and 3; the current room is copied into room[][] when it is
 * entered.
 */
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "game.h"
#include "banks.h"
#include "orb.h"

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

/* slots 2 and 3 back to bank 5 (the code bank is at 0x0000, so the SDK's
   bank copies, which give slots 0 and 1 back to the ROM, aren't used) */
static void windows_out(void)
{
    ZXN_WRITE_MMU2(10);
    ZXN_WRITE_MMU3(11);
}

void world_reset(void)
{
    uint8_t p;
    for (p = 0; p < 2; p++) {
        ZXN_WRITE_MMU2(WORLD_BANK * 2 + p);
        ZXN_WRITE_MMU3(WORK_BANK * 2 + p);
        dma_copy(WIN3, WIN2, 8192);
    }
    windows_out();
}

/* the work bank in slots 2 and 3 (0x4000-0x7FFF) */
static uint8_t *work_in(void)
{
    ZXN_WRITE_MMU2(WORK_BANK * 2);
    ZXN_WRITE_MMU3(WORK_BANK * 2 + 1);
    return WIN2 + room_off;
}

void world_load_room(WORD rx, WORD ry)
{
    room_off = (uint16_t)(ry * WORLD_W + rx) * (ROOM_W * ROOM_H);
    memcpy(room, work_in(), sizeof(room));
    windows_out();
}

void world_set_tile(WORD tx, WORD ty, UBYTE t)
{
    room[ty][tx] = t;
    room_dirty = 1;
    work_in()[ty * ROOM_W + tx] = t;
    windows_out();
}
