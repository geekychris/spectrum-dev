/*
 * fixmath.h - 16-bit fixed-point helpers for physics and geometry, fast on
 * a Z80: a 16 x 16 -> 32 multiply (on the Next the Z80N's MUL, through
 * z88dk's library), the shifted products used with Q14 (16384 = 1.0) and
 * Q8 values, a 32-bit integer square root and a Q14 division.
 * Boing Pinball's physics (16-bit units, see its pbz.h) is built on these.
 * On a host compiler (not __SDCC) the same functions are inline C, so code
 * using them can be tested on the host with the same results.
 */
#ifndef ZX_FIXMATH_H
#define ZX_FIXMATH_H
#include <stdint.h>

#ifdef __SDCC
extern int32_t mul16(int16_t a, int16_t b);         /* a * b */
extern int16_t mulq14(int16_t a, int16_t b);        /* (a * b) >> 14 (floor) */
extern int16_t mulq8(int16_t a, int16_t b);         /* (a * b) >> 8 (floor) */
extern uint16_t isqrt32(uint32_t v);                /* floor(sqrt(v)) */
#else
static inline int32_t mul16(int16_t a, int16_t b) { return (int32_t)a * b; }
static inline int16_t mulq14(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 14); }
static inline int16_t mulq8(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 8); }
static inline uint16_t isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint16_t)r;
}
#endif
/* (a << 14) / d: a / d in Q14, for |a| <= |d| (unit vectors) */
#ifdef __SDCC
extern int16_t div_q14(int16_t a, int16_t d);
#else
static inline int16_t div_q14(int16_t a, int16_t d) { return (int16_t)(((int32_t)a << 14) / d); }
#endif

#endif
