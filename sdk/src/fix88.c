/* 8.8 fixed point and integer helpers (see zxgame.h). */
#include <stdint.h>
#include "zxgame.h"

fix8 fix_mul(fix8 a, fix8 b)
{
    return (fix8)(((int32_t)a * b) >> 8);
}

fix8 fix_div(fix8 a, fix8 b)
{
    /* spelled out: zsdcc got ((int32_t)a << 8) / b wrong (returned 0) */
    int32_t n = a;
    int32_t d = b;
    n *= 256;
    return (fix8)(n / d);
}

uint16_t isqrt16(uint16_t v)
{
    uint16_t r = 0, bit = 0x4000;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* atan(t) for t = 0..1 in 1/32 steps, as angle units (256 per turn) */
static const uint8_t atan_tab[33] = {
    0, 1, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 25, 26, 27, 28, 29, 29, 30, 31, 31, 32
};

uint8_t fix_atan2(int16_t dy, int16_t dx)
{
    uint16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    uint8_t a;
    if (ax == 0 && ay == 0) return 0;
    if (ax >= ay) a = atan_tab[(uint16_t)(((uint32_t)ay * 32) / ax)];
    else a = 64 - atan_tab[(uint16_t)(((uint32_t)ax * 32) / ay)];
    if (dx < 0) a = 128 - a;
    if (dy < 0) a = (uint8_t)(256 - a);
    return a;
}
