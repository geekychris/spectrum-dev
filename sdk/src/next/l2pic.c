/* A wide picture scrolled sideways on Layer 2's 320 x 256 mode (l2pic.h). */
#include <stdint.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "l2pic.h"

uint16_t l2pic_copied;
static uint8_t index_page, row0;
static uint16_t ncols;
static int16_t held = -32767;            /* the picture column at the ring's view start, or none */

void l2pic_init(uint8_t ipage, uint16_t n, uint8_t r0)
{
    index_page = ipage;
    ncols = n;
    row0 = r0;
    held = -32767;
}

void l2pic_reset(void) { held = -32767; }

void l2pic_scroll(uint16_t s)
{
    ZXN_WRITE_REG(0x16, (uint8_t)s);
    ZXN_WRITE_REG(0x71, (uint8_t)(s >> 8) & 1);
}

void l2pic_column(uint16_t lx, uint16_t x)
{
    uint8_t s2 = ZXN_READ_REG(0x52), s3 = ZXN_READ_REG(0x53), page;
    const uint8_t *e;
    uint16_t addr;
    if (x >= ncols) return;
    ZXN_WRITE_REG(0x52, index_page);
    e = (const uint8_t *)0x4000 + x * 3;
    page = e[0];
    addr = e[1] | (uint16_t)e[2] << 8;
    ZXN_WRITE_REG(0x52, page);
    ZXN_WRITE_REG(0x53, l2w_bank * 2 + (uint8_t)(lx >> 5));
    l2pic_rle((uint8_t *)0x6000 + ((lx & 31) << 8) + row0, (const uint8_t *)addr, (uint8_t)(256 - row0));
    ZXN_WRITE_REG(0x52, s2);
    ZXN_WRITE_REG(0x53, s3);
    l2pic_copied++;
}

/* x mod 320 without %: zsdcc can call z88dk's xcc __moduint for it, which
   takes its arguments in registers and returns x / 320 (CLAUDE.md) */
static uint16_t ring(uint16_t x)
{
    while (x >= 320) x -= 320;
    return x;
}

static void copy(int16_t from, int16_t to)    /* picture columns from..to-1 */
{
    int16_t x;
    uint16_t lx;
    if (from < 0) from = 0;
    lx = ring((uint16_t)from);
    for (x = from; x < to; x++) {
        l2pic_column(lx, (uint16_t)x);
        if (++lx == 320) lx = 0;
    }
}

uint16_t l2pic_view(int16_t x)
{
    if (x < 0) x = 0;
    if (held == -32767 || x >= held + 320 || x <= held - 320) copy(x, x + 320);
    else if (x > held) copy(held + 320, x + 320);
    else if (x < held) copy(x, held);
    held = x;
    return ring((uint16_t)x);
}
