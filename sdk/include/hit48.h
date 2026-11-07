/*
 * hit48.h - circle collision tests against a list, in assembly
 * (sdk/src/hit48.asm): build the list of targets once a frame, then test
 * each shot / the player against it.
 *
 *   hit_circle rocks[24];  ... fill x, y, r (and id: anything, e.g. the slot)
 *   uint8_t k = hit_first(bx, by, 2, rocks, n);   // 0xFF: no hit
 *
 * A circle i is hit when |dx| < r + r_i, |dy| < r + r_i and
 * dx^2 + dy^2 < (r + r_i)^2 (integer coordinates, any 16-bit range; the
 * differences must fit 16 bits). Radius sums up to 127. About 70 T-states
 * per circle that is too far in x, ~110 if only y rejects it, a few hundred
 * for the squares.
 */
#ifndef HIT48_H
#define HIT48_H
#include <stdint.h>

typedef struct { int16_t x, y; uint8_t r, id; } hit_circle;

/* the first circle of list[0..n) touching the circle (x, y, r), or 0xFF */
extern uint8_t hit_first(int16_t x, int16_t y, uint8_t r, const hit_circle *list, uint8_t n);

#endif
