/*
 * nextlibtest - the Next library modules: DMA, copper, 320 x 256 Layer 2 with
 * column-RLE pieces, sprite groups with masking, tilemap text, palette
 * effects, the TurboSound stream player, the isometric sort. test.py checks
 * the log and the screen.
 */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nextcopper.h"
#include "l2rle.h"
#include "nextspr.h"
#include "nexttile.h"
#include "palfx.h"
#include "aystream.h"
#include "isosort.h"
#include "zxlog.h"

/* a 4 x 6 piece: column 0 rows 1-2, column 1 rows 0-5, column 2 none,
   column 3 rows 0 and 4-5 (two runs) */
static const uint8_t piece[] = {
    4, 6,
    1, 2, 1, 1, 0xFF,
    0, 6, 2, 2, 2, 2, 2, 2, 0xFF,
    0xFF,
    0, 1, 3, 3, 2, 3, 3, 0xFF
};
static uint8_t lut[256];
static uint8_t buf[512], img[4 * 256];

/* a stream: frame 0 sets tone A 0x123 volume 12, 3 frames wait, end */
static const uint8_t stream[] = {
    0xFF, 0xFF,
    0x83, 0x41, 0x23, 0x01, 0x3E, 0x0C, 3,
    0x00, 0x80
};

static const uint8_t glyph_a[8] = { 0x00, 0x3C, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00 };
static const uint16_t pal3[3] = { RGB9(1, 2, 3), RGB9(7, 0, 0), RGB9(4, 4, 4) };

static uint8_t l2_at(int16_t x, uint8_t y)
{
    uint8_t v = l2w_column(x)[y];
    l2w_done();
    return v;
}

static uint16_t line(void)                      /* the raster line now */
{
    return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);
}

static uint8_t ay_read(uint8_t chip, uint8_t r)
{
    z80_outp(0xFFFD, 0xFF - chip);
    z80_outp(0xFFFD, r);
    return z80_inp(0xFFFD);
}

int main(void)
{
    uint16_t i;
    uint8_t ord[3];
    IsoBox b[3];

    next_init();
    frame_init();
    ZXN_WRITE_REG(0x15, 0x13);                  /* U S L, sprites on, over the border */

    /* DMA */
    for (i = 0; i < 512; i++) buf[i] = (uint8_t)i;
    dma_copy(img, buf, 300);
    dma_fill(img + 300, 0x5A, 200);
    zx_log("lib: dma copy=%u fill=%u edge=%u\n", memcmp(img, buf, 300) == 0, img[300] == 0x5A && img[499] == 0x5A,
           img[500] == 0);

    /* Layer 2 320 x 256 */
    l2w_slot = 2;                               /* draw through 0x4000 (bank 5 page 10 back after) */
    l2w_restore = 10;
    l2w_init(9);
    l2w_clear(4);
    l2w_fill(300, 250, 20, 6, 5);
    for (i = 0; i < 256; i++) lut[i] = (uint8_t)(i + 0x10);
    l2w_blit_rle(100, 50, piece, 0);
    l2w_blit_rle(318, 60, piece, lut);          /* columns 2, 3 off the screen */
    l2w_span(10, 10, 5, 0x7F, 0x80);
    zx_log("lib: l2w %u %u %u %u %u %u %u %u\n", l2_at(100, 50), l2_at(100, 51), l2_at(101, 55), l2_at(103, 50),
           l2_at(103, 54), l2_at(103, 53), l2_at(318, 61), l2_at(319, 60));
    zx_log("lib: l2w fill=%u,%u span=%u,%u\n", l2_at(319, 255), l2_at(299, 255), l2_at(14, 10), l2_at(15, 10));

    /* palettes: Layer 2 entries 4, 5 and the second palette at half */
    palfx_load(PAL_L2, 4, 3, pal3);
    palfx_load_fx(PAL_L2 | PAL_SECOND, 4, 3, pal3, 4, 0);
    ZXN_WRITE_REG(0x43, 0x10);
    ZXN_WRITE_REG(0x40, 5);
    i = ZXN_READ_REG(0x41);
    ZXN_WRITE_REG(0x43, 0x50);
    ZXN_WRITE_REG(0x40, 5);
    zx_log("lib: pal l2[5]=%02x second=%02x fx=%03x\n", i, ZXN_READ_REG(0x41), pal_fx(RGB9(6, 2, 7), 7, RGB9(1, 1, 1)));
    ZXN_WRITE_REG(0x43, 0x00);

    /* copper: from L2 row 128 down, show the second (half) palette */
    copper_begin();
    copper_wait(L2Y_TO_LINE(4), 0);             /* top border first: the list restarts at line 248 */
    copper_move(0x7F, 1);
    copper_move(0x43, 0x00);                    /* Layer 2's first palette shown */
    copper_wait(L2Y_TO_LINE(100), 0);
    copper_move(0x7F, 2);
    copper_wait(L2Y_TO_LINE(128), 0);
    copper_move(0x7F, 3);
    copper_move(0x43, 0x04);                    /* from row 128: the second (half) palette */
    zx_log("lib: copper n=%u\n", copper_end());
    {
        /* the raster line where each value shows up (user register 0x7F) */
        uint8_t last = 0, v, k = 0;
        uint16_t l;
        while (k < 4) {
            v = ZXN_READ_REG(0x7F);
            l = line();
            if (v != last) {
                if (k) zx_log("lib: cop %u at %u\n", v, l);   /* the first may be mid-frame */
                last = v;
                k++;
            }
        }
    }

    /* sprite group: 2 x 2 patterns, the piece masks the top-left corner */
    spr_init();
    palfx_load(PAL_SPR, 0, 3, pal3);
    memset(img, 2, sizeof(img));                /* all pen 2 (grey) */
    sprg_mask_rle(img, 2, 2, 0, 0, piece);
    sprg_mask_rect(img, 2, 2, 20, 20, 40, 40);  /* bottom-right 12 x 12 */
    zx_log("lib: mask %u %u %u %u %u\n", img[16], img[0], img[3], img[3 * 256 + 4 * 16 + 4], img[3 * 256 + 3 * 16 + 3]);
    {
        /* a 1 x 2 group: the piece at (-1, 13) clips at the left and crosses
           into the pattern below; piece column 1 (rows 0-5) -> group column 0, rows 13..18; column 3
           (rows 0 and 4-5) -> group column 2, rows 13 and 17-18 */
        static uint8_t g[512];
        memset(g, 2, sizeof(g));
        sprg_mask_rle(g, 1, 2, -1, 13, piece);
        zx_log("lib: mask2 %u %u %u %u %u %u\n", g[13 * 16], g[14 * 16], g[15 * 16 + 0], g[256 + 2 * 16], g[256 + 3 * 16], g[13 * 16 + 2]);
    }
    sprg_upload(0, img, 4);
    zx_log("lib: next sprite=%u\n", sprg_show(0, 200, 100, 2, 2, 0, 0));
    sprg_hide_from(4);

    /* tilemap: "A" in red on row 2 */
    tm_init();
    tm_font(1, glyph_a, 1, 1, 0);
    tm_glyph2x(2, glyph_a, 1, 2);
    tm_pal(3, 1, RGB9(7, 0, 0));
    tm_pal(3, 2, RGB9(0, 0, 7));
    tm_put(5, 2, 1, 3);
    tm_put(10, 4, 2, 3);
    tm_put(11, 4, 3, 3);
    tm_put(10, 5, 4, 3);
    tm_put(11, 5, 5, 3);

    /* TurboSound streams: copy one to page 40 and play it on chip 1 */
    ZXN_WRITE_REG(0x52, 40);
    memcpy((void *)0x4000, stream, sizeof(stream));
    ZXN_WRITE_REG(0x52, 10);
    as_init();
    as_play(1, 40, 0x4000, 0);
    as_tick();
    zx_log("lib: ay r0=%u r1=%u r8=%u busy=%u\n", ay_read(1, 0), ay_read(1, 1), ay_read(1, 8), as_busy(1));
    as_atten(1, 4);
    zx_log("lib: ay atten r8=%u\n", ay_read(1, 8));
    for (i = 0; i < 5; i++) as_tick();
    zx_log("lib: ay end busy=%u r8=%u\n", as_busy(1), ay_read(1, 8));

    /* isometric order: a block at the back, one in front, a thing on top */
    iso_set(&b[0], 0, 0, 0, 10, 10, 10, 0, 0, 20, 20);          /* front */
    iso_set(&b[1], 0, 0, 20, 10, 10, 30, 0, 0, 20, 20);         /* behind (+z) */
    iso_set(&b[2], 0, 10, 0, 10, 20, 10, 0, 0, 20, 20);         /* on top of b0 */
    iso_sort(b, 3, ord, 0);
    zx_log("lib: iso %u %u %u behind=%u\n", ord[0], ord[1], ord[2], iso_behind(&b[1], &b[0], 0));

    zx_log("lib: done\n");
    for (;;) frame_wait();
}
