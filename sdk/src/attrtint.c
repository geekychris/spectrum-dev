/* attrtint.h: tinting cells under moving objects. */
#include <stdint.h>
#include <string.h>
#include "attrtint.h"
#include "zxgame.h"

uint8_t tint_map[96];
uint8_t tint_plain;

#define ATTR ((uint8_t *)0x5800)

/* tint_capture is attrtint_capture.asm */

uint8_t tint_is_plain(uint16_t offset)
{
    return (tint_map[offset >> 3] & gfx_bit[offset & 7]) != 0;
}

void tint_mark(uint8_t col, uint8_t row, uint8_t w, uint8_t plain)
{
    uint16_t o = (uint16_t)row * 32 + col;
    for (; w; w--, o++) {
        uint8_t *p = tint_map + (o >> 3), m = gfx_bit[o & 7];
        *p = plain ? (*p | m) : (*p & ~m);
    }
}

/* every plain cell of t gets attr */
static void fill(const tint_area *t, uint8_t attr)
{
    uint8_t r, c;
    uint16_t o = (uint16_t)t->row * 32 + t->col;
    for (r = t->h; r; r--, o += 32 - t->w)
        for (c = t->w; c; c--, o++)
            if (tint_map[o >> 3] & gfx_bit[o & 7]) ATTR[o] = attr;
}

void tint_restore(const tint_area *t)
{
    fill(t, tint_plain);
}

void tint_apply(const tint_area *t, uint8_t attr)
{
    fill(t, attr);
}

void tint_place(tint_area *t, uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t attr)
{
    if (col > 31) { w = 0; col = 0; }
    if (row > 23) { h = 0; row = 0; }
    if (col + w > 32) w = 32 - col;
    if (row + h > 24) h = 24 - row;
    t->col = col;
    t->row = row;
    t->w = w;
    t->h = h;
    fill(t, attr);
}

void tint_rect_off(tint_area *t)
{
    fill(t, tint_plain);
    t->w = t->h = 0;
}

void tint_cell(uint16_t offset, uint8_t attr)
{
    if (tint_is_plain(offset)) ATTR[offset] = attr;
}

void tint_uncell(uint16_t offset)
{
    if (tint_is_plain(offset)) ATTR[offset] = tint_plain;
}
