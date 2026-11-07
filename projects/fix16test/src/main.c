/*
 * fix16test - the SDK's fix16 (16.16 fixed point as the amiga3d engine
 * has it, and fixmath's isqrt32 under it) on the Next. Logs results;
 * test.py checks them against a Python model.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "fix16.h"
#include "fixmath.h"
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

int main(void)
{
    uint16_t i;
    uint8_t k;
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
    zx_log("ff: done\n");
    for (;;) frame_wait();
}
