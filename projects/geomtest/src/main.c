/*
 * geomtest - the SDK's geometry modules for the Next: l2poly (polygons and
 * spans on Layer 2). Draws into the back buffer, reads pixels back and logs
 * them; test.py checks the log.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "l2poly.h"
#include "zxlog.h"

static uint8_t px(uint8_t x, uint8_t y)        /* back buffer pixel */
{
    uint8_t v, s = ZXN_READ_REG(0x53);
    ZXN_WRITE_REG(0x53, l2_back * 2 + (y >> 5));
    v = ((uint8_t *)0x6000)[((uint16_t)(y & 31) << 8) + x];
    ZXN_WRITE_REG(0x53, s);
    return v;
}

static uint16_t row_count(uint8_t y, uint8_t c)  /* pixels of colour c in row y */
{
    uint16_t n = 0;
    uint8_t x = 0;
    do { if (px(x, y) == c) n++; } while (++x);
    return n;
}

static int16_t first(uint8_t y, uint8_t c)
{
    uint16_t x;
    for (x = 0; x < 256; x++) if (px(x, y) == c) return x;
    return -1;
}

static int16_t last(uint8_t y, uint8_t c)
{
    int16_t x;
    for (x = 255; x >= 0; x--) if (px(x, y) == c) return x;
    return -1;
}

static const int16_t tri[] = { 10, 10, 50, 10, 10, 50 };            /* right angle at 10,10 */
static const int16_t big[] = { -5000, -3000, 9000, -2000, 8000, 4000, -6000, 3000 };
static const int16_t tall[] = { 100, -400, 140, -400, 140, 600, 100, 600 };   /* dy 1000 */
static const int16_t slant[] = { 0, -100, 40, -100, 240, 100, 200, 100 };     /* starts above row 0 */
static const int16_t cw[] = { 60, 120, 60, 160, 20, 160, 20, 120 };            /* other winding */

int main(void)
{
    next_init();
    frame_init();
    l2_init();
    l2_clear(0);
    l2p_begin();

    l2p_poly(tri, 3, 1);
    zx_log("geo: tri r10=%d..%d r30=%d..%d r49=%d..%d r50=%u\n", first(10, 1), last(10, 1),
           first(30, 1), last(30, 1), first(49, 1), last(49, 1), row_count(50, 1));

    l2p_top = 60; l2p_bot = 70; l2p_xmin = 8; l2p_xmax = 247;
    l2p_poly(big, 4, 2);
    zx_log("geo: clip r59=%u r60=%u..%d r69=%u r70=%u\n", row_count(59, 2), row_count(60, 2),
           last(60, 2), row_count(69, 2), row_count(70, 2));
    l2p_top = 0; l2p_bot = 192; l2p_xmin = 0; l2p_xmax = 255;

    l2p_poly(tall, 4, 3);
    zx_log("geo: tall r0=%d..%d r191=%d..%d\n", first(0, 3), last(0, 3), first(191, 3), last(191, 3));

    l2_clear(0);
    l2p_poly(slant, 4, 4);
    zx_log("geo: slant r0=%d..%d r99=%d..%d\n", first(0, 4), last(0, 4), first(99, 4), last(99, 4));

    l2p_poly(cw, 4, 5);
    zx_log("geo: cw r120=%d..%d r159=%d..%d r160=%u\n", first(120, 5), last(120, 5),
           first(159, 5), last(159, 5), row_count(160, 5));

    l2p_span(180, -10, 300, 6);               /* DMA, clipped both ends */
    l2p_span(181, 5, 9, 7);                   /* unrolled */
    l2p_span(182, 0, 255, 8);                 /* 256 */
    l2p_span(183, 30, 20, 9);                 /* empty */
    zx_log("geo: span %u %d..%d %u %u %u\n", row_count(180, 6), first(181, 7), last(181, 7),
           row_count(182, 8), row_count(183, 9), px(4, 181));
    l2p_end();
    zx_log("geo: done\n");
    for (;;) frame_wait();
}
