/*
 * fix16.h - 16.16 fixed point as the amiga3d engine has it (a3d_fix.h), so
 * an amiga3d game's rules can be ported to the Z80 and give the original's
 * results bit for bit (Marble Tilt's state hashes match its tests).
 *
 *   fix                 int32_t, 65536 = 1.0 (FONE, FX(1.5), FROMI(3), FINT)
 *   fmul(a, b)          (int64)a * b >> 16 (floor), truncated to 32 bits:
 *                       the original's FMUL
 *   fdiv(a, b)          ((int64)a << 16) / b, truncating toward zero: FDIV
 *                       (b != 0)
 *   fsqrt(a), fhypot(a, b)
 *                       the engine's square roots, lengths in 32-bit
 *                       arithmetic (scaled down until the squares fit)
 *   a3_isqrt(v)         floor(sqrt(v)) of a 32-bit value
 *   a3_sin(a), a3_cos(a)  the engine's 16-bit angles (65536 = a turn),
 *                       interpolated from its 256-entry table, * 65536
 *   a3_angle(rad)       radians (16.16) -> 16-bit angle
 *   fsin(rad), fcos(rad)
 *   fhypot3(a, b, c)    a 3-D length (fix16_hyp3.c)
 *   fatan2(y, x)        radians, -pi..pi, as Math.atan2; fasin(x); and in the
 *                       engine's 16-bit angles a3_atan_ratio(num, den) (den > 0,
 *                       |num| <= den) and a3_atan2(x, y) (from +y towards +x)
 *                       (fix16_atan.c)
 *   fexp_neg(x)         e^-x for x >= 0 (fix16_exp.c)
 *
 * On the Z80 the multiplies use z88dk's 16 x 16 -> 32 (the Z80N's MUL on the
 * Next): fmul is four of them, about 1,200 T-states; fdiv a 32-bit division
 * and 16 more quotient bits, 2,000-4,000. On a host compiler (not __SDCC)
 * the same functions are inline C with 64-bit arithmetic, so code using
 * them can be tested on the host: tools/test_fix16.c (built with -DFIX16_Z80_C) checks
 * the Z80 C against it, projects/sdktestnext against Python.
 */
#ifndef ZX_FIX16_H
#define ZX_FIX16_H
#include <stdint.h>

typedef int32_t fix;
#define FONE       65536L
#define FX(x)      ((fix)((x) * 65536.0))
#define FROMI(i)   ((fix)(i) * 65536L)
#define FINT(a)    ((a) >> 16)

extern const int32_t a3_sin_tab[256];
int32_t a3_sin(uint16_t a);
int32_t a3_cos(uint16_t a);
uint32_t a3_isqrt(uint32_t v);
fix fsqrt(fix a);
fix fhypot(fix a, fix b);
fix fsin(fix rad);
fix fcos(fix rad);
fix fhypot3(fix a, fix b, fix c);
int16_t a3_atan_ratio(int32_t num, int32_t den);
uint16_t a3_atan2(int32_t x, int32_t y);
fix fatan2(fix y, fix x);
fix fasin(fix x);
fix fexp_neg(fix x);

#if defined(__SDCC) || defined(FIX16_Z80_C)
extern uint32_t mulu16(uint16_t a, uint16_t b);    /* a * b */
fix fmul(fix a, fix b);
fix fdiv(fix a, fix b);
int16_t a3_angle(fix rad);
#else
static inline fix fmul(fix a, fix b) { return (fix)(((int64_t)a * b) >> 16); }
static inline fix fdiv(fix a, fix b) { return (fix)(((int64_t)a * 65536) / b); }
static inline int16_t a3_angle(fix rad) { return (int16_t)(((int64_t)rad * 683565276LL) >> 32); }
#endif

#endif
