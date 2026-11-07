/*
 * vec48.h - vector shapes for line48.h: rotate and scale int8 vertex lists,
 * and rotation tables built once for shapes that turn every frame.
 *
 *   static const vec_pt ship[5] = {{10, 0}, {-7, -6}, {-4, -2}, {-4, 2}, {-7, 6}};
 *   static vec_pt ship_rot[128 * 5];
 *   vec_rot_table(ship, 5, 205, 7, ship_rot);      // 128 angles, scale 205/256 = 0.8
 *   line_poly(x, y, vec_rot_at(ship_rot, 5, 7, angle), 5, 1);
 *
 * Angles are 0..255 for a full turn (0 = +x, 64 = +y: down the screen), as
 * fix_sin/fix_cos in zxgame.h. Scale is /256 (256 = 1.0). A rotated point is
 * round((x cos - y sin) * scale, (x sin + y cos) * scale).
 *
 * Building a table costs about 2,500 T-states a point (once, at start-up):
 * 128 angles x 8 points is ~0.7 s.
 */
#ifndef VEC48_H
#define VEC48_H
#include <stdint.h>
#include "line48.h"

/* n points of src turned by angle and scaled -> dst */
extern void vec_rotate(const vec_pt *src, uint8_t n, uint8_t angle, uint16_t scale, vec_pt *dst);
/* n points scaled (no rotation) -> dst */
extern void vec_scale(const vec_pt *src, uint8_t n, uint16_t scale, vec_pt *dst);
/* (1 << bits) angles of src (bits 0..8): dst holds (1 << bits) * n points */
extern void vec_rot_table(const vec_pt *src, uint8_t n, uint16_t scale, uint8_t bits, vec_pt *dst);
/* the table entry nearest to angle */
#define vec_rot_at(table, n, bits, angle) \
    ((table) + (uint16_t)(n) * \
     (uint8_t)((((uint8_t)(angle) + (128 >> (bits))) >> (8 - (bits))) & ((1 << (bits)) - 1)))

#endif
