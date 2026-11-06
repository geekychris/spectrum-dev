/* Layer 2 320 x 256 extras (see l2rle.h); the RLE blit is l2rle_blit.asm. */
#include <stdint.h>
#include <arch/zxn.h>
#include "l2rle.h"

void l2w_clear(uint8_t c)
{
    uint8_t p;
    for (p = 0; p < 10; p++) {
        ZXN_WRITE_REG(0x50 + l2w_slot, l2w_bank * 2 + p);
        dma_fill((uint8_t *)((uint16_t)l2w_slot << 13), c, 8192);
    }
    l2w_done();
}

void l2w_plot(int16_t x, uint8_t y, uint8_t c)
{
    if (x < 0 || x >= 320) return;
    l2w_column(x)[y] = c;
    l2w_done();
}

void l2w_span(int16_t x, uint8_t y, uint8_t len, uint8_t and_mask, uint8_t or_mask)
{
    uint8_t *p = 0;
    if (x < 0) {
        if (-x >= len) return;
        len += (uint8_t)x;
        x = 0;
    }
    if (x >= 320) return;
    if (x + len > 320) len = (uint8_t)(320 - x);
    for (; len; len--, x++) {
        if (!p || (x & 31) == 0) p = l2w_column(x) + y;
        *p = (*p & and_mask) | or_mask;
        p += 256;
    }
    l2w_done();
}
