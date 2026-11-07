/* tm_glyphnx (see nexttile.h): in a file of its own, so games that don't
   call it don't carry its 1K pixel buffer. */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "nexttile.h"

#define MAP  ((uint8_t *)0x4000)            /* bank 5 offset 0, in slot 2 */
#define DEFS 0x0A00                         /* the definitions' offset in bank 5 */

static uint8_t s2;

/* only slot 2 is used, so strings and data anywhere else stay readable */
static void map_in(void)
{
    s2 = ZXN_READ_REG(0x52);
    ZXN_WRITE_REG(0x52, 10);
}

static void map_out(void) { ZXN_WRITE_REG(0x52, s2); }

/* tile t's definition, mapped into slot 2 (a tile never crosses an 8K page) */
static uint8_t *def_ptr(uint16_t t)
{
    uint16_t off = DEFS + (t << 5);
    ZXN_WRITE_REG(0x52, 10 + (off >> 13));
    return (uint8_t *)0x4000 + (off & 0x1FFF);
}

static uint8_t px[32][32];

/* the glyph's pixels as n x n blocks of pen, offset by (off, off), clipped */
static void blocks(const uint8_t *glyph, uint8_t n, uint8_t size, uint8_t off, uint8_t pen)
{
    uint8_t gx, gy, y0, x0, i, j;
    for (gy = 0, y0 = off; gy < 8; gy++, y0 += n) {
        uint8_t bits = glyph[gy];
        for (gx = 0, x0 = off; bits; gx++, x0 += n, bits <<= 1) {
            if (!(bits & 0x80)) continue;
            for (j = y0; j < y0 + n && j < size; j++)
                for (i = x0; i < x0 + n && i < size; i++) px[j][i] = pen;
        }
    }
}

void tm_glyphnx(uint8_t tile, const uint8_t *glyph, uint8_t n, uint8_t fg, uint8_t shadow, uint8_t off)
{
    static uint8_t def[32];
    uint8_t *d, x, y, t, size;
    if (n < 1 || n > 4) return;
    size = n << 3;
    memset(px, 0, sizeof(px));
    if (shadow) blocks(glyph, n, size, off, shadow);
    blocks(glyph, n, size, 0, fg);
    for (t = 0; t < n * n; t++) {
        uint8_t ox = (t % n) << 3, oy = (t / n) << 3;
        d = def;
        for (y = 0; y < 8; y++) {
            const uint8_t *r = &px[oy + y][ox];
            for (x = 0; x < 8; x += 2) *d++ = r[x] << 4 | r[x + 1];
        }
        map_in();
        memcpy(def_ptr(tile + t), def, 32);
        map_out();
    }
}

