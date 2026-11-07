/* l2scroll.h: banded horizontal scrolling on Layer 2's 320 x 256 mode, each
   band a colring.h ring of 320 pixel columns. */
#include <stdint.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextcopper.h"
#include "nextsplit.h"
#include "colring.h"
#include "l2scroll.h"

typedef struct {
    uint8_t y0, y1;
    l2s_fill_fn fill;
    colring ring;
} l2s_band_t;

static l2s_band_t bands[L2S_MAX];
static uint8_t nbands;

void l2s_reset(void)
{
    nbands = 0;
}

/* colring's callback: ctx is the band */
static void fill_col(void *ctx, uint16_t col, uint32_t wx)
{
    l2s_band_t *b = (l2s_band_t *)ctx;
    b->fill((uint8_t)(b - bands), wx, l2w_column(col));
}

uint8_t l2s_band(uint8_t y0, uint8_t y1, l2s_fill_fn fill)
{
    l2s_band_t *b = &bands[nbands];
    b->y0 = y0;
    b->y1 = y1;
    b->fill = fill;
    cr_init(&b->ring, L2S_W, 0, fill_col, b);
    return nbands++;
}

void l2s_band_wrap(uint8_t n, uint32_t width)
{
    bands[n].ring.wrap = width;
}

void l2s_goto(uint8_t n, uint32_t x)
{
    cr_goto(&bands[n].ring, x);
    l2w_done();
}

void l2s_move(uint8_t n, uint32_t x)
{
    cr_move(&bands[n].ring, x);
    l2w_done();
}

uint16_t l2s_col(uint8_t n, uint32_t wx)
{
    return cr_col(&bands[n].ring, wx);
}

void l2s_redraw(uint8_t n, uint32_t wx)
{
    uint16_t col = cr_col(&bands[n].ring, wx);
    if (col == CR_NONE) return;
    fill_col(&bands[n], col, wx);
    l2w_done();
}

uint16_t l2s_offset(uint8_t n)
{
    return bands[n].ring.off;
}

static void scroll_moves(uint16_t off)
{
    copper_move(0x16, (uint8_t)off);
    copper_move(0x71, (uint8_t)(off >> 8));
}

void l2s_copper(void)
{
    uint8_t n;
    uint16_t next = 0;                   /* the first row not yet given a scroll */
    for (n = 0; n < nbands; n++) {
        l2s_band_t *b = &bands[n];
        if (b->y0 > next) {              /* rows before this band: scroll 0 */
            if (next) split_row((uint8_t)next);
            scroll_moves(0);
        }
        if (b->y0) split_row(b->y0);
        scroll_moves(b->ring.off);
        next = (uint16_t)b->y1 + 1;
    }
    if (next && next < 256) {            /* rows after the last band: scroll 0 */
        split_row((uint8_t)next);
        scroll_moves(0);
    }
}
