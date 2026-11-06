/*
 * isosort.h - draw order for isometric scenes made of boxes (Knight Lore /
 * Head over Heels style), the painter's dependency sort.
 *
 * World axes: x and z run into the screen (away from the viewer), y is up.
 * Box A is behind box B (drawn first) when a separating axis puts it further
 * along +x or +z, or below B. Two boxes whose screen rectangles don't meet
 * need no order. When neither separates, a depth key decides (bigger =
 * further). A depth-first walk over "must come first" edges gives the order.
 *
 *   iso_set(b, x0, y0, z0, x1, y1, z1, sx0, sy0, sx1, sy1)
 *                          fill a box and its screen rectangle; key =
 *                          (x0 + x1 + z0 + z1) * 17 - (y0 + y1) * 14 (32-bit)
 *   iso_order(a, b, eps)   1: a before b, -1: b before a, 0: either
 *   iso_behind(a, b, eps)  1 if a must be drawn before b and they overlap on
 *                          screen (e.g. "is this scenery in front of the
 *                          sprite": iso_behind(sprite, scenery))
 *   iso_sort(boxes, n, order)
 *                          order[0..n-1] = box indices, back to front
 *                          (n <= ISO_MAX)
 *
 * Units are up to the game (16-bit coordinates; eps in the same units).
 */
#ifndef ISOSORT_H
#define ISOSORT_H

#include <stdint.h>

#define ISO_MAX 40

typedef struct {
    int16_t x0, y0, z0, x1, y1, z1;     /* world box */
    int16_t sx0, sy0, sx1, sy1;         /* screen rectangle (inclusive) */
    int32_t key;
} IsoBox;

extern void iso_set(IsoBox *b, int16_t x0, int16_t y0, int16_t z0, int16_t x1, int16_t y1,
                    int16_t z1, int16_t sx0, int16_t sy0, int16_t sx1, int16_t sy1);
extern int8_t iso_order(const IsoBox *a, const IsoBox *b, int16_t eps);
extern uint8_t iso_rects_meet(const IsoBox *a, const IsoBox *b);
extern uint8_t iso_behind(const IsoBox *a, const IsoBox *b, int16_t eps);
extern void iso_sort(const IsoBox *boxes, uint8_t n, uint8_t *order, int16_t eps);

#endif
