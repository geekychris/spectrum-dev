/* a3d.h - port: the parts of the amiga3d engine's a3d.h that Ace Pilot 2's
   rules use (a3d_math.c's atan2, hypot and rand; the rest is fix16.h) */
#ifndef A3D_H
#define A3D_H
#include <stdint.h>
#include "a3d_fix.h"

int a3_atan2(int32_t x, int32_t y);    /* the angle of (x, y) from +y towards +x, 16-bit */
int32_t a3_hypot(int32_t x, int32_t y); /* sqrt(x x + y y), |x|, |y| < 2^30 */
uint32_t a3_rand(uint32_t *state);
fix fhypot3(fix a, fix b, fix c);		/* a3d_fix.c */
#endif
