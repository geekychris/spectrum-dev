/* tmap + plat cases, built for the Spectrum (main.c) and the host (test.py
   compiles this with host_main.c): both must log the same lines. */
#include <stdint.h>
#include "tmap.h"
#include "plat.h"
#include "rooms_data.h"
#include "cases.h"

static uint8_t cells[RM_W * RM_H];
static const uint8_t solid[4] = { 0, 1, 0, 0 };

static void run(const char *name, int16_t x, int16_t y, int16_t vx, int16_t vy, uint8_t steps)
{
    plat_body b;
    b.x = x; b.y = y; b.vx = vx; b.vy = vy; b.on_ground = 0;
    while (steps--) {
        b.vx = vx;
        plat_move_x(&b);
        plat_move_y(&b, 1, 6);
    }
    CASE_LOG("plat: %s x=%d y=%d vy=%d ground=%u\n", name, b.x, b.y, b.vy, b.on_ground);
}

void cases(void)
{
    uint8_t c = 0, r = 0, found, a1, a2, a3;
    uint16_t n1, n2;
    /* z88dk's printf evaluates its arguments right to left: results first */
    tmap_init(cells, RM_W, RM_H);
    n1 = tmap_load(rm_maps[0]);
    CASE_LOG("tmap: unpacked=%u\n", n1);
    n1 = tmap_count(1);
    n2 = tmap_count(3);
    CASE_LOG("tmap: walls=%u keys=%u\n", n1, n2);
    found = tmap_find(2, &c, &r);
    CASE_LOG("tmap: spawn=%u found=%u,%u\n", found, c, r);
    n1 = tmap_replace(2, 0);
    n2 = tmap_count(2);
    a1 = tmap_at(-1, 0, 9);
    a2 = tmap_at(3, 2, 9);
    a3 = tmap_at(8, 0, 9);
    CASE_LOG("tmap: replaced=%u now=%u at=%u,%u,%u\n", n1, n2, a1, a2, a3);
    plat_shift = 4;
    plat_bw = plat_bh = 16;
    plat_solid = solid;
    plat_outside = 1;
    a1 = plat_kind_at(16, 16);
    a2 = plat_kind_at(48, 32);
    a3 = plat_kind_at(-1, 5);
    found = plat_kind_at(200, 20);
    CASE_LOG("plat: kind=%u,%u,%u,%u\n", a1, a2, a3, found);
    /* lands from y = 61: the original's snap puts it on top of the tile its
       top was in (y = 48), not on the floor (64) - Miner Meteor's README */
    run("fall", 20, 16, 0, 0, 11);
    run("wall", 32, 64, 2, 0, 60);               /* walks right into the wall block */
    run("left", 64, 64, -2, 0, 60);              /* and left into the outer wall */
    run("ceiling", 48, 50, 0, -12, 1);           /* jumps into the ledge above */
    run("ledge", 30, 0, 1, 0, 30);               /* falls onto the ledge, slides */
}
