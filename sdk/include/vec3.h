/*
 * vec3.h - 3D vectors in Q14 fixed point (16384 = 1.0) with 16-bit
 * components, for unit directions, rotation matrices and short positions.
 * Products are exact: each one is 32 bits, sums are 32 bits, and results are
 * shifted right by 14 (floor, like a C `>> 14` on a long). So a game written
 * for a 32-bit CPU with Q14 longs gets the same numbers, bit for bit, as long
 * as its vectors' components fit 16 bits (Planet Chomp's logic is checked
 * this way against the original on the host).
 *
 *   v3_dot(a, b)          (ax bx + ay by + az bz) >> 14
 *   v3_dot32(a, b)        the sum before the shift (32 bits)
 *   v3_cross(r, a, b)     each component (p - q) >> 14
 *   v3_scale(r, a, s)     each component (a * s) >> 14
 *   v3_mat(r, m, a)       r = M a: r.x = v3_dot(m[0], a) ... (rows of m)
 *   v3_norm(r, a)         a scaled to length 16384 (see below); a zero
 *                         vector gives (0, 16384, 0)
 *   v3_norm32(r, x, y, z) the same for 32-bit components
 *
 * v3_norm is a common fixed-point normalise, done exactly: scale the vector
 * by powers of two until its largest component is in 4096..30000 (at most
 * 8 doublings), len = isqrt32(x^2 + y^2 + z^2), inv = 2^28 / len, then each
 * component (c * inv) >> 14.
 *
 * r may be the same vector as an input. Uses static scratch memory: not
 * re-entrant (don't call it from an interrupt).
 *
 * The Z80 code is vec3.asm (MUL on the Next through fixmath's multiply, a
 * shift-and-add loop on a 48K); on a host compiler (not __SDCC) vec3.c gives
 * the same functions in C, for host tests and simulations.
 * Test: projects/sdktestnext (vec3 checks) against the C versions.
 */
#ifndef ZX_VEC3_H
#define ZX_VEC3_H
#include <stdint.h>

typedef struct { int16_t x, y, z; } vec3;

#define V3_ONE 16384

extern int16_t v3_dot(const vec3 *a, const vec3 *b);
extern int32_t v3_dot32(const vec3 *a, const vec3 *b);
extern void v3_cross(vec3 *r, const vec3 *a, const vec3 *b);
extern void v3_scale(vec3 *r, const vec3 *a, int16_t s);
extern void v3_mat(vec3 *r, const vec3 *m, const vec3 *a);
extern void v3_norm(vec3 *r, const vec3 *a);
extern void v3_norm32(vec3 *r, int32_t x, int32_t y, int32_t z);

#define v3_set(r, X, Y, Z) do { (r)->x = (X); (r)->y = (Y); (r)->z = (Z); } while (0)
#define v3_add(r, a, b) do { (r)->x = (a)->x + (b)->x; (r)->y = (a)->y + (b)->y; (r)->z = (a)->z + (b)->z; } while (0)
#define v3_sub(r, a, b) do { (r)->x = (a)->x - (b)->x; (r)->y = (a)->y - (b)->y; (r)->z = (a)->z - (b)->z; } while (0)
#define v3_zero(a) (((a)->x | (a)->y | (a)->z) == 0)

#endif
