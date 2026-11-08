/* texviewtest: the SDK's texview (see test.py) */
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "texview.h"
#include "zxlog.h"

#define TEX_PAGE 64                      /* the texture: 3 pages */
#define OUT_BANK 20                      /* where the views are drawn */
#define VIEW_PAGE 80                     /* bank 40: the view (tools/gen_data.py) */

static void draw(int16_t x, int16_t y, uint8_t off)
{
    tv_page = VIEW_PAGE;
    tv_cx = x;
    tv_cy = y;
    tv_off = off;
    tv_bank = OUT_BANK;
    tv_tex = TEX_PAGE;
    tv_draw();
    zx_log("tv: drew %d,%d off=%u pixels=%u\n", x, y, off, tv_pixels);
}

int main(void)
{
    uint8_t p, y;
    uint16_t i;
    next_init();
    dma_fill_banks(OUT_BANK, 0, 3);
    /* the texture: texel (u, v) = (u + 3v) & 31 */
    for (p = 0; p < 3; p++) {
        ZXN_WRITE_REG(0x53, TEX_PAGE + p);
        for (i = 0; i < 8192; i++) ((uint8_t *)0x6000)[i] = (uint8_t)(((i & 255) + 3 * (p * 32 + (i >> 8))) & 31);
    }
    ZXN_WRITE_REG(0x53, 11);
    draw(60, 40, 0);
    draw(250, 185, 17);                  /* off the right and the bottom */
    draw(5, -3, 200);                    /* off the left and the top */
    tv_rows = 100;
    draw(150, 95, 3);                    /* cut at row 100 */
    tv_rows = 192;
    tv_bank = OUT_BANK;
    tv_or_span(100, 10, 10, 0x40);       /* over the last disc's row 100 (cut: left alone) */
    tv_or_span(110, 100, 8, 0x40);
    /* the window by hand: a fill and a copy turned round */
    ZXN_WRITE_REG(0x13, OUT_BANK);
    z80_outp(0x123B, 0x0B | (1 << 6));   /* third 1: rows 64-127 */
    tv_fill((uint8_t *)((120 - 64) << 8), 0x33, 256);
    ZXN_WRITE_REG(0x50, TEX_PAGE);       /* the texture for reading */
    ZXN_WRITE_REG(0x51, TEX_PAGE + 1);
    tv_copy((const uint8_t *)(5 << 8 | 90), (uint8_t *)((126 - 64) << 8 | 7), 40, 1);
    tv_copy((const uint8_t *)(6 << 8 | 10), (uint8_t *)((127 - 64) << 8 | 7), 40, 0);
    z80_outp(0x123B, 0x02);
    ZXN_WRITE_REG(0x50, 0xFF);
    ZXN_WRITE_REG(0x51, 0xFF);
    y = 0;
    zx_log("tv: done %u\n", y);
    for (;;) intrinsic_halt();
    return 0;
}
