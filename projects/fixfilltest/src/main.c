/*
 * fixfilltest - the SDK's fix16 (16.16 fixed point as the amiga3d engine
 * has it, and fixmath's isqrt32 under it) and l2fill (convex polygons on Layer 2) on the Next. Logs
 * results; test.py checks them against Python models.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "fix16.h"
#include "fixmath.h"
#include "l2fill.h"
#include "zxlog.h"

static uint32_t s = 12345;
static int32_t rnd(void)                     /* as test.py's: small, medium, large, edge values */
{
    uint32_t r;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    r = s;
    switch ((uint8_t)r & 3) {
    case 0: return (int32_t)(r >> 8) - 8388608L;
    case 1: return (int32_t)(r >> 15) - 65536L;
    case 2: return (int32_t)r;
    }
    return (r & 4) ? (int32_t)0x80000000UL + (int32_t)((r >> 3) & 3) : 0x7FFFFFFFL - (int32_t)((r >> 3) & 3);
}

static uint32_t h = 2166136261UL;
static void mix(int32_t v) { h ^= (uint32_t)v; h *= 16777619UL; }

static uint8_t px(uint8_t x, uint8_t y)
{
    uint8_t v, sv = ZXN_READ_REG(0x53);
    ZXN_WRITE_REG(0x53, l2_back * 2 + (y >> 5));
    v = ((uint8_t *)0x6000)[((uint16_t)(y & 31) << 8) + x];
    ZXN_WRITE_REG(0x53, sv);
    return v;
}

/* first and last x of colour c in row y, -1 none */
static void row(uint8_t y, uint8_t c, int16_t *a, int16_t *b)
{
    uint16_t x;
    *a = *b = -1;
    for (x = 0; x < 256; x++)
        if (px((uint8_t)x, y) == c) {
            if (*a < 0) *a = (int16_t)x;
            *b = (int16_t)x;
        }
}

static const int16_t tri[] = { 10, 10, 50, 10, 10, 50 };
static const int16_t big[] = { -5000, -3000, 9000, -2000, 8000, 4000, -6000, 3000 };
static const int16_t tall[] = { 100, -400, 140, -400, 140, 600, 100, 600 };    /* dy 1000: the division */
static const int16_t slant[] = { 3, -100, 43, -100, 243, 100, 203, 100 };      /* starts above row 0 */
static const int16_t cw[] = { 60, 120, 60, 160, 20, 160, 20, 120 };             /* the other winding */
static const int16_t oct[] = { 150, 140, 170, 132, 190, 140, 198, 160, 190, 180, 170, 188, 150, 180, 142, 160 };
static const int16_t thin[] = { 30, 170, 31, 170, 90, 190, 89, 190 };
static const struct { const int16_t *p; uint8_t n; uint8_t rows[4]; } shapes[] = {
    { tri, 3, { 10, 30, 49, 50 } },
    { tall, 4, { 0, 100, 191, 190 } },
    { slant, 4, { 0, 50, 99, 100 } },
    { cw, 4, { 120, 140, 159, 160 } },
    { oct, 8, { 132, 140, 170, 187 } },
    { thin, 4, { 170, 175, 182, 189 } },
};

int main(void)
{
    uint16_t i;
    uint8_t k, j;
    next_init();
    frame_init();
    /* fix16: 2000 pairs */
    for (i = 0; i < 2000; i++) {
        int32_t a = rnd(), b = rnd();
        mix(fmul(a, b));
        if (b) mix(fdiv(a, b));
        mix(a3_angle(a));
        mix(fhypot(a >> 8, b >> 8));
        mix(fsqrt(a & 0x7FFFFFFFL));
        mix(fsin(a >> 12));
    }
    {
        static const uint32_t v[] = { 4294967295UL, 4026531840UL, 3000000000UL, 2147495993UL, 1073741824UL,
                                      1073741823UL, 2147483648UL, 3221225472UL, 65536UL, 999999999UL };
        for (k = 0; k < 10; k++) zx_log("ff: isqrt %lu %lu\n", v[k], (uint32_t)isqrt32(v[k]));
    }
    zx_log("ff: fix16 %08lX\n", h);
    /* l2fill */
    l2_init();
    l2_clear(0);
    l2f_begin();
    for (k = 0; k < sizeof(shapes) / sizeof(shapes[0]); k++) l2f_poly(shapes[k].p, shapes[k].n, 1 + k);
    l2f_top = 60;
    l2f_bot = 70;
    l2f_poly(big, 4, 9);
    l2f_top = 0;
    l2f_bot = 192;
    l2f_end();
    for (k = 0; k < sizeof(shapes) / sizeof(shapes[0]); k++)
        for (j = 0; j < 4; j++) {
            int16_t a, b;
            row(shapes[k].rows[j], 1 + k, &a, &b);
            zx_log("ff: poly %u row %u %d..%d\n", k, shapes[k].rows[j], a, b);
        }
    {
        int16_t a, b, c, d, e, f;
        row(59, 9, &a, &b);
        row(60, 9, &c, &d);
        row(70, 9, &e, &f);
        zx_log("ff: clip r59=%d r60=%d..%d r70=%d\n", a, c, d, e);
    }
    zx_log("ff: done\n");
    for (;;) frame_wait();
}
