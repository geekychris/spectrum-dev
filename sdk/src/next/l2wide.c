/*
 * Layer 2 in its 320 x 256 mode (see nextgfx.h, l2w_*): five 16K banks,
 * column-major (byte x * 256 + y), covering the whole display, border
 * included. Drawing maps the 8K page holding a column into MMU slot
 * l2w_slot and puts l2w_restore back when done.
 */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "font5.h"

uint8_t l2w_bank = 9, l2w_slot = 3, l2w_restore = 11;

void l2w_init(uint8_t bank)
{
    l2w_bank = bank;
    ZXN_WRITE_REG(0x70, 0x10);          /* 320 x 256, 8 bpp */
    ZXN_WRITE_REG(0x12, bank);
    ZXN_WRITE_REG(0x1C, 0x01);          /* reset the Layer 2 clip index */
    ZXN_WRITE_REG(0x18, 0);             /* clip x 0..159 (pairs of pixels), y 0..255 */
    ZXN_WRITE_REG(0x18, 159);
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 255);
    ZXN_WRITE_REG(0x16, 0);             /* no scroll */
    ZXN_WRITE_REG(0x17, 0);
    ZXN_WRITE_REG(0x71, 0);
    z80_outp(0x123B, 0x02);             /* visible */
}

uint8_t *l2w_column(uint16_t x)
{
    ZXN_WRITE_REG(0x50 + l2w_slot, l2w_bank * 2 + (x >> 5));
    return (uint8_t *)((uint16_t)l2w_slot << 13) + ((x & 31) << 8);
}

void l2w_done(void)
{
    ZXN_WRITE_REG(0x50 + l2w_slot, l2w_restore);
}

void l2w_fill(uint16_t x, uint8_t y, uint16_t w, uint16_t h, uint8_t colour)
{
    for (; w; w--, x++) memset(l2w_column(x) + y, colour, h);
    l2w_done();
}

void l2w_text5(uint16_t x, uint8_t y, const char *s, uint8_t colour, uint8_t sx, uint8_t sy, uint8_t gap)
{
    for (; *s; s++) {
        const uint8_t *gl = font5_glyph(*s);
        uint8_t col, r, k, j;
        if (!gl) { x += (FONT5_W + 1) * sx; continue; }
        for (col = 0; col < FONT5_W; col++)
            for (k = 0; k < sx; k++, x++) {
                uint8_t *p = l2w_column(x) + y;
                for (r = 0; r < FONT5_H; r++, p += sy)
                    if (gl[r] & (0x80 >> col))
                        for (j = 0; j < sy - gap; j++) p[j] = colour;
            }
        x += sx;
    }
    l2w_done();
}
