/*
 * td_fix.h - the amiga3d engine's fixed point that Tank Duel's rules use
 * (a3d_fix.h, a3d_math.c), for the Z80N: 16.16 values, 16-bit angles.
 * port: FMUL and FDIV are functions (the originals used 64-bit
 * intermediates), with the same results: floor((a * b) / 65536) and
 * (a * 65536) / b truncated, both cut to 32 bits.
 */
#ifndef TD_FIX_H
#define TD_FIX_H

#include <stdint.h>

typedef int32_t fix;
#define FONE        65536L
#define FX(x)       ((fix)((x) * 65536.0))
#define FINT(a)     ((a) >> 16)
#define FROMI(i)    ((fix)(i) * 65536L)
extern fix fmul(fix a, fix b);
extern fix fdiv(fix a, fix b);
#define FMUL(a, b)  fmul((a), (b))
#define FDIV(a, b)  fdiv((a), (b))

extern int32_t a3_sin(uint16_t a);
#define a3_cos(a)   a3_sin((uint16_t)(a) + 16384)
extern uint16_t a3_atan2(int32_t x, int32_t y);
extern fix fhypot(fix a, fix b);

#endif
