/*
 * texttest - the tmtext module (sdk tmtext.h): text slots on the tilemap
 * that are rewritten only when they change, and big letters made from the
 * same font into a pool of tiles. test.py checks the log and the screen.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "tmtext.h"
#include "palfx.h"
#include "zxlog.h"

static const uint8_t glyph_a[8] = { 0x00, 0x3C, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00 };
static uint8_t font96[96][8];           /* only 'A' has a glyph */
static tmt_slot slots[3];

/* a tilemap cell's tile (bank 5, through slot 2) */
static uint8_t map_at(uint8_t col, uint8_t row)
{
    uint8_t v, s2 = ZXN_READ_REG(0x52);
    ZXN_WRITE_REG(0x52, 10);
    v = *(uint8_t *)(0x4000 + ((uint16_t)row * 40 + col) * 2);
    ZXN_WRITE_REG(0x52, s2);
    return v;
}

static void map_set(uint8_t col, uint8_t row, uint8_t t)
{
    uint8_t s2 = ZXN_READ_REG(0x52);
    ZXN_WRITE_REG(0x52, 10);
    *(uint8_t *)(0x4000 + ((uint16_t)row * 40 + col) * 2) = t;
    ZXN_WRITE_REG(0x52, s2);
}

int main(void)
{
    uint8_t a, b, same, shrunk, big0, big3, centre;
    char n[12];
    next_init();
    frame_init();
    tm_init();
    ZXN_WRITE_REG(0x15, 0x13);                  /* the tilemap on top */
    tm_pal(3, 1, RGB9(7, 0, 0));
    tm_pal(3, 2, RGB9(0, 0, 0));
    memcpy(font96['A' - 32], glyph_a, 8);
    tm_glyphnx(20 + 'A' - 32, glyph_a, 1, 1, 0, 0);   /* the font's tiles: 20.. ('A' = 53) */
    tmt_init(slots, 3, font96[0], 20, 150);         /* big letters from tile 150 */

    tmt_say(0, 30, 10, "AA", 3, 1, 0);
    a = map_at(30, 10);
    b = map_at(31, 10);
    map_set(30, 10, 0);
    tmt_say(0, 30, 10, "AA", 3, 1, 0);              /* unchanged: not rewritten */
    same = map_at(30, 10);
    tmt_say(0, 30, 10, "A", 3, 1, 0);               /* changed: the old cells erased first */
    shrunk = map_at(31, 10);
    tmt_say(1, 4, 20, "A", 3, 2, 1);                /* big: 2 x 2 tiles from the pool, shadowed */
    big0 = map_at(4, 20);
    big3 = map_at(5, 21);
    tmt_say(2, TMT_CENTRE, 2, "AAAA", 3, 1, 0);     /* centred: columns 18..21 */
    centre = map_at(18, 2);
    tmt_num(n, -1234567L);
    zx_log("txt: say %u %u same=%u shrunk=%u big=%u,%u centre=%u num=%s\n", a, b, same, shrunk, big0, big3, centre, n);
    zx_log("txt: done\n");
    for (;;) frame_wait();
}
