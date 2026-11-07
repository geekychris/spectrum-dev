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

/* a * b (low 16 bits), fast when |a| is small: a shift-and-add per bit of
   |a| (fixmath_muls.asm); e.g. small vertex coordinates times a Q8 sine */
#ifdef __SDCC
extern int16_t muls(int16_t a, int16_t b);
#else
static inline int16_t muls(int16_t a, int16_t b) { return (int16_t)(a * b); }
#endif

/* sine and cosine in Q14 (16384 = 1.0), 256 steps per turn (0 = 0, 64 = a
   quarter turn): precise enough for thrust and rotation with mul16 / mulq14
   (zxgame.h's fix_sin is 8.8) */
extern const int16_t fix_sin14_tab[256];
#define fix_sin14(a)  (fix_sin14_tab[(uint8_t)(a)])
#define fix_cos14(a)  (fix_sin14_tab[(uint8_t)((a) + 64)])

/* 2D shapes: rotate n points (x, y int8 pairs) by an angle (256 a turn, 0 =
   no turn; with screen y down, angle 64 takes (0, -1) "up" to (1, 0)) and
   scale x and y by scale_x / 256 and scale_y / 256 (Q8: 256 = 1.0), rounded
   to int8: x' = (x cos - y sin) sx, y' = (x sin + y cos) sy. rot2d_set once
   per angle, then rot2d for any number of point lists (rot2d.asm: ~1,000
   T-states a point with coordinates up to +-15). out may not overlap in. */
extern void rot2d_set(uint8_t angle, int16_t scale_x, int16_t scale_y);
#ifdef __SDCC
extern void rot2d(const int8_t *in, int8_t *out, uint8_t n);
#else
extern int16_t rot2d_kcx, rot2d_ksx, rot2d_kcy, rot2d_ksy;
static inline void rot2d(const int8_t *in, int8_t *out, uint8_t n)
{
    for (; n; n--, in += 2, out += 2) {
        int16_t x = in[0], y = in[1];
        out[0] = (int8_t)((x * rot2d_kcx - y * rot2d_ksx + 128) >> 8);
        out[1] = (int8_t)((x * rot2d_ksy + y * rot2d_kcy + 128) >> 8);
    }
}
#endif

#endif
