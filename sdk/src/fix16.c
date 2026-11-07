/*
 * fix16.c - 16.16 fixed point as the amiga3d engine computes it (fix16.h):
 * square roots, lengths and the sine table's interpolation, and on the Z80
 * the 64-bit products and quotients built from 16 x 16 multiplies.
 */
#include "fix16.h"

#if defined(__SDCC) && !defined(FIX16_Z80_C)
#include "fixmath.h"
uint32_t a3_isqrt(uint32_t v) { return isqrt32(v); }     /* the same, in asm */
#else
uint32_t a3_isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
#endif

fix fsqrt(fix a)
{
    uint32_t v = (uint32_t)a, r;
    uint8_t k = 0;
    if (a <= 0) return 0;
    while (v < (1UL << 28) && k < 15) { v <<= 2; k++; }
    r = a3_isqrt(v);
    return (fix)(k <= 8 ? r << (8 - k) : r >> (k - 8));
}

fix fhypot(fix a, fix b)
{
    uint32_t x = (uint32_t)(a < 0 ? -a : a), y = (uint32_t)(b < 0 ? -b : b);
    uint8_t sh = 0;
    while ((x | y) >= 46000UL) { x >>= 1; y >>= 1; sh++; }
#if defined(__SDCC)
    return (fix)(a3_isqrt(mulu16((uint16_t)x, (uint16_t)x) + mulu16((uint16_t)y, (uint16_t)y)) << sh);
#else
    return (fix)(a3_isqrt(x * x + y * y) << sh);
#endif
}

int32_t a3_sin(uint16_t a)
{
    uint8_t i = (uint8_t)(a >> 8), f = (uint8_t)a;
    int32_t s0 = a3_sin_tab[i], s1 = a3_sin_tab[(uint8_t)(i + 1)];
    return s0 + (((s1 - s0) * f) >> 8);
}

int32_t a3_cos(uint16_t a) { return a3_sin(a + 16384); }
fix fsin(fix rad) { return a3_sin((uint16_t)a3_angle(rad)); }
fix fcos(fix rad) { return a3_cos((uint16_t)a3_angle(rad)); }

#if defined(__SDCC) || defined(FIX16_Z80_C)
/* fmul, fdiv: fix16.asm on the Z80 (fix16_model.h is their C model, for
   the host test) */
#if !defined(__SDCC)
#include "fix16_model.h"
#endif

/* (rad * 683565276) >> 32: bits 32..47 are all a3_sin uses */
int16_t a3_angle(fix rad)
{
    return (int16_t)(fmul(rad, 683565276L) >> 16);
}
#endif
