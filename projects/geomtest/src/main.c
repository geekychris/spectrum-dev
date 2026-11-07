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
#include "persp.h"
#include "zxlog.h"

static uint16_t lines(void)                 /* raster lines since some frame (wraps) */
{
    uint8_t f = frame_ticks;
    uint16_t l = ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);
    if (f != frame_ticks) return lines();
    return (uint16_t)(f & 127) * 312 + l;
}

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

    l2p_end();                                /* l2_clear uses the DMA too */
    l2_clear(0);
    l2p_begin();
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
    /* bands: two rails crossing nothing, a road between rails 1 and 2 over the grass */
    {
        static const int16_t xt[] = { -500, 100, 156, 900 }, xb[] = { -900, 60, 196, 1500 };
        static const uint8_t spans[] = { 0, 3, 10, 1, 2, 11 };
        l2p_end();
        l2_clear(0);
        l2p_begin();
        l2p_band(100, 140, xt, xb, 4, spans, 2);
        zx_log("geo: band r99=%u r100=%d..%d %u r139=%d..%d %u r140=%u\n", row_count(99, 10),
               first(100, 11), last(100, 11), row_count(100, 10), first(139, 11), last(139, 11),
               row_count(139, 10), row_count(140, 10));
        l2p_band(30, 10, xt, xb, 4, spans, 2);       /* edges given the other way up */
        zx_log("geo: band2 r10=%d..%d r29=%d..%d\n", first(10, 11), last(10, 11), first(29, 11), last(29, 11));
        {
            /* rails no span uses (0 and 2) are skipped: the same as a band of the two used */
            static const int16_t x2t[] = { 100, 900 }, x2b[] = { 60, 1500 };
            static const uint8_t sp3[] = { 3, 1, 13 }, sp2[] = { 0, 1, 14 };
            int16_t a0, a1, b0, b1;
            l2p_band(50, 70, xt, xb, 4, sp3, 1);
            a0 = first(50, 13); a1 = last(69, 13);
            l2p_band(50, 70, x2t, x2b, 2, sp2, 1);
            b0 = first(50, 14); b1 = last(69, 14);
            zx_log("geo: band3 %d..%d %d..%d left13=%u\n", a0, a1, b0, b1, row_count(50, 13) + row_count(69, 13));
        }
        l2p_rect(-5, 3, 250, 300, 12);
        zx_log("geo: rect %u %u %d\n", row_count(0, 12), row_count(2, 12), first(3, 12));
    }
    l2p_end();
    persp_fov(144);
    {
        static const int16_t pts[] = { 0, 0, 100, -50, -30000, 20000 };
        static int16_t out[6];
        uint16_t s = persp_scale(1000);
        persp_xy(pts, 3, 20, -92, s, 128, 59, out);
        zx_log("geo: persp s=%u m=%d,%d %d,%d %d,%d %d,%d\n", s, persp_mul(1000, s), persp_mul(-1000, s),
               out[0], out[1], out[2], out[3], out[4], out[5]);
        {
            /* persp_xs / persp_ys against persp_x1 / persp_y1, and persp_mul at its edges */
            static const int16_t vs[] = { 0, 1, -1, 7, -7, 4095, -4095, 4097, -4097, 1000, -1000,
                                          16000, -16000, 32767, -32767, -32768, 300, -301 };
            static const uint16_t ss[] = { 1, 589, 4096, 4097, 20000, 32767 };
            static int16_t xo[18], yo[18];
            uint8_t i, j, bad = 0;
            int32_t sum = 0;
            for (j = 0; j < 6; j++) {
                persp_xs(vs, 18, 37, ss[j], 128, xo);
                persp_ys(vs, 18, -45, ss[j], 40, yo);
                for (i = 0; i < 18; i++) {
                    if (xo[i] != persp_x1(vs[i], 37, ss[j], 128)) bad++;
                    if (yo[i] != persp_y1(vs[i], -45, ss[j], 40)) bad++;
                    sum += persp_mul(vs[i], ss[j]) * (int32_t)(i + 1) * (j + 1);
                }
            }
            zx_log("geo: persp batch bad=%u mulsum=%ld\n", bad, (long)sum);
        }
        persp_k = 0x12345UL;                     /* low bits set: both division paths */
        zx_log("geo: scale %u %u %u\n", persp_scale(7), persp_scale(300), persp_scale(1));
    }
    /* timings: interrupts (1/50 s = 560000 T-states at 28 MHz) over many calls,
       logged as T-states per call */
    {
        uint16_t k;
        static const int16_t sq[] = { 10, 10, 60, 10, 60, 40, 10, 40 };
        static const uint8_t lens[] = { 1, 10, 30, 39, 40, 100, 255 };
        uint8_t j;
        l2p_begin();
#define TIME(name, n, stmt) do { uint8_t f0 = frame_ticks; for (k = 0; k < (n); k++) stmt; \
            zx_log("geo: time " name " %lu T\n", (uint32_t)(uint8_t)(frame_ticks - f0) * 560000UL / (n)); } while (0)
        for (j = 0; j < sizeof(lens); j++) {
            uint8_t f0 = frame_ticks, x1 = lens[j] - 1;
            for (k = 0; k < 2000; k++) l2p_span(50, 0, x1, 1);
            zx_log("geo: time span %u px %lu T\n", lens[j], (uint32_t)(uint8_t)(frame_ticks - f0) * 560000UL / 2000);
        }
        TIME("poly 51x30", 200, l2p_poly(sq, 4, 2));
        TIME("scale", 4000, persp_scale(1000 + k));
        TIME("mul", 4000, persp_mul(1000 + k, 589));
        TIME("loop", 4000, (void)0);
        l2p_end();
    }
    zx_log("geo: done\n");
    for (;;) frame_wait();
}
