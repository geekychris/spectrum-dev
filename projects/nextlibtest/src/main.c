/*
 * nextlibtest - the Next library modules: DMA, copper, 320 x 256 Layer 2 with
 * column-RLE pieces, sprite groups with masking, tilemap text, palette
 * effects, the TurboSound stream player, the isometric sort, convex polygons
 * with a depth plane (l2wpoly), the 4-bit sprite list with depth masking
 * (nextsprl), big tilemap glyphs (tm_glyphnx). test.py checks
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
#include "l2wpoly.h"
#include "nextsprl.h"
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

static uint8_t pat4[128], pat4m[128];
static const int16_t tri[6] = { 40, 40, 60, 40, 40, 60 };
static const int16_t tri_r[6] = { 40, 60, 60, 40, 40, 40 };   /* the other way round */

/* a depth-plane byte (pages 60..) */
static uint8_t z_at(int16_t x, uint8_t y)
{
    uint8_t v;
    ZXN_WRITE_REG(0x50, 60 + (x >> 5));
    v = *(uint8_t *)(((uint16_t)(x & 31) << 8) + y);
    ZXN_WRITE_REG(0x50, 0xFF);
    return v;
}

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

    /* l2wpoly: rectangles, a triangle both ways round, clipping, the depth
       plane (pages 60..), copying from another plane (pages 70..) */
    l2wp_target(18, 0);
    l2wp_rect(20, 20, 30, 25, 7);
    l2wp_poly(tri, 3, 8);
    zx_log("lib: l2wp rect %u %u %u %u tri %u %u %u %u\n", l2_at(20, 20), l2_at(29, 24), l2_at(30, 20), l2_at(20, 25),
           l2_at(41, 41), l2_at(58, 41), l2_at(58, 42), l2_at(60, 40));
    l2wp_poly(tri_r, 3, 9);
    zx_log("lib: l2wp tri2 %u %u %u\n", l2_at(41, 41), l2_at(58, 41), l2_at(58, 42));
    l2wp_clip(100, 100, 110, 110);
    l2wp_rect(90, 90, 120, 120, 9);
    l2wp_clip(0, 0, 320, 256);
    zx_log("lib: l2wp clip %u %u %u %u\n", l2_at(100, 100), l2_at(99, 100), l2_at(110, 105), l2_at(105, 110));
    l2wp_fill(60, 190, 20, 230, 50, 0xFF);
    l2wp_target(18, 60);
    l2wp_key = 50;
    l2wp_rect(200, 30, 210, 40, 10);
    l2wp_fill(70, 0, 100, 10, 110, 0x33);
    l2wp_copy(18, 70, 0, 100, 10, 110);
    zx_log("lib: l2wp depth %u %u %u copy %u %u\n", z_at(205, 35), z_at(215, 35), l2_at(205, 35), l2_at(5, 105), l2_at(10, 105));

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

    /* nextsprl: a 4-bit pattern masked by the depth plane (50 at 200..209,
       30..39): at (195, 25) with key 60 the 10 x 10 corner goes, key 40 none */
    memset(pat4, 0x11, sizeof(pat4));
    {
        uint16_t a = spl_zmask(pat4m, pat4, 195, 25, 60, 60), b;
        uint8_t in = pat4m[5 * 8 + 2] & 15, out = pat4m[4 * 8 + 2] >> 4;   /* (5, 5) behind, (4, 4) not */
        b = spl_zmask(pat4m, pat4, 195, 25, 40, 60);
        zx_log("lib: spl zmask %u %u %u %u\n", a, in, out, b);
    }
    spl_patterns(10, pat4, 1);
    pal_set(2, 17, RGB9(7, 0, 0));                  /* sprite palette offset 1, pen 1: red */
    spl_first = 4;                                  /* after the sprite group's 4 */
    spl_begin();
    spl_add(240, 140, 10, 1, 0);
    spl_add(-20, 140, 10, 1, 0);                    /* off the screen: skipped */
    spl_end();
    zx_log("lib: spl count=%u\n", spl_count);

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
    tm_glyphnx(6, glyph_a, 3, 1, 0, 0);             /* "A" at 3x: tiles 6..14 */
    for (i = 0; i < 9; i++) tm_put(20 + i % 3, 6 + i / 3, 6 + i, 3);

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
