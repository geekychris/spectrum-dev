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

extern void l2w_span_core(uint8_t *p, uint16_t n, uint16_t and_or);   /* l2rle_span.asm */

void l2w_span(int16_t x, uint8_t y, uint8_t len, uint8_t and_mask, uint8_t or_mask)
{
    uint8_t n;
    if (x < 0) {
        if (-x >= len) return;
        len += (uint8_t)x;
        x = 0;
    }
    if (x >= 320) return;
    if (x + len > 320) len = (uint8_t)(320 - x);
    while (len) {                       /* a piece per 8K page (32 columns) */
        n = 32 - (x & 31);
        if (n > len) n = len;
        l2w_span_core(l2w_column(x) + y, n, (uint16_t)or_mask << 8 | and_mask);
        x += n;
        len -= n;
    }
    l2w_done();
}
