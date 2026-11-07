/*
 * persp.h - perspective projection for pseudo-3D on the Z80N: one division
 * per depth (a scale), then a MUL per coordinate.
 *
 *   persp_fov(fov)              the focal length in pixels: x_screen = x * fov / z
 *   s = persp_scale(z)          fov * 4096 / z (z >= 16 keeps it below 32768)
 *   persp_mul(v, s)             (v * s) >> 12: v at depth z, in pixels
 *   persp_xy(pts, n, bx, by, s, cx, cy, out)
 *                               n points (x, y pairs) offset by (bx, by), all at
 *                               the depth s was made for, to screen pairs:
 *                               cx + persp_mul(x + bx, s), cy - persp_mul(y + by, s),
 *                               clamped to +-16000 (what l2poly takes)
 *   persp_x1(x, bx, s, cx)      one point: cx + persp_mul(x + bx, s), clamped
 *   persp_y1(y, by, s, cy)      one point: cy - persp_mul(y + by, s), clamped
 *   persp_xs(xs, n, bx, s, cx, out)
 *                               the same for x alone (n offsets on a horizontal line)
 *
 * Everything shares one depth per call, which is what a road made of
 * cross-sections needs: every point of a cross-section is at the same z.
 * On a host compiler (not __SDCC) the same functions are C, for host tests.
 * Test: projects/geomtest.
 */
#ifndef PERSP_H
#define PERSP_H

#include <stdint.h>

#define PERSP_CLAMP 16000

extern uint32_t persp_k;                 /* fov << 12 */
extern void persp_fov(int16_t fov);
extern uint16_t persp_scale(uint16_t z);
extern void persp_xy(const int16_t *pts, uint8_t n, int16_t bx, int16_t by, uint16_t s,
                     int16_t cx, int16_t cy, int16_t *out);
extern int16_t persp_x1(int16_t x, int16_t bx, uint16_t s, int16_t cx);
extern int16_t persp_y1(int16_t y, int16_t by, uint16_t s, int16_t cy);
extern void persp_xs(const int16_t *xs, uint8_t n, int16_t bx, uint16_t s, int16_t cx, int16_t *out);
#ifdef __SDCC
extern int16_t persp_mul(int16_t v, uint16_t s);
#else
static inline int16_t persp_mul(int16_t v, uint16_t s)
{
    int32_t r = ((int32_t)v * s) >> 12;
    return (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
}
#endif

#endif
