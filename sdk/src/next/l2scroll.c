/* l2scroll.h: banded horizontal scrolling on Layer 2's 320 x 256 mode. */
#include <stdint.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextcopper.h"
#include "nextsplit.h"
#include "l2scroll.h"

typedef struct {
    uint8_t y0, y1;
    uint32_t x;          /* world x at the left edge */
    uint16_t off;        /* x mod 320: the column at the left edge */
    l2s_fill_fn fill;
} l2s_band_t;

static l2s_band_t bands[L2S_MAX];
static uint8_t nbands;

void l2s_reset(void)
{
    nbands = 0;
}

uint8_t l2s_band(uint8_t y0, uint8_t y1, l2s_fill_fn fill)
{
    l2s_band_t *b = &bands[nbands];
    b->y0 = y0;
    b->y1 = y1;
    b->x = 0;
    b->off = 0;
    b->fill = fill;
    return nbands++;
}

static void draw(uint8_t n, uint16_t col, uint32_t wx)
{
    bands[n].fill(n, wx, l2w_column(col));
}

void l2s_goto(uint8_t n, uint32_t x)
{
    l2s_band_t *b = &bands[n];
    uint16_t c, col;
    b->x = x;
    b->off = (uint16_t)(x % L2S_W);
    for (c = 0, col = b->off; c < L2S_W; c++) {
        draw(n, col, x + c);
        if (++col == L2S_W) col = 0;
    }
    l2w_done();
}

void l2s_move(uint8_t n, uint32_t x)
{
    l2s_band_t *b = &bands[n];
    if (x == b->x) return;
    if (x > b->x ? x - b->x >= L2S_W : b->x - x >= L2S_W) {
        l2s_goto(n, x);
        return;
    }
    while (b->x < x) {                   /* right: world x + 320 enters where x left */
        draw(n, b->off, b->x + L2S_W);
        b->x++;
        if (++b->off == L2S_W) b->off = 0;
    }
    while (b->x > x) {                   /* left: world x - 1 enters at the left */
        b->x--;
        b->off = b->off ? b->off - 1 : L2S_W - 1;
        draw(n, b->off, b->x);
    }
    l2w_done();
}

void l2s_redraw(uint8_t n, uint32_t wx)
{
    l2s_band_t *b = &bands[n];
    uint32_t d;
    uint16_t col;
    if (wx < b->x) return;
    d = wx - b->x;
    if (d >= L2S_W) return;
    col = b->off + (uint16_t)d;
    if (col >= L2S_W) col -= L2S_W;
    draw(n, col, wx);
    l2w_done();
}

uint16_t l2s_offset(uint8_t n)
{
    return bands[n].off;
}

void l2s_copper(void)
{
    uint8_t n;
    for (n = 0; n < nbands; n++) {
        l2s_band_t *b = &bands[n];
        if (b->y0) split_row(b->y0);
        copper_move(0x16, (uint8_t)b->off);
        copper_move(0x71, (uint8_t)(b->off >> 8));
    }
}
