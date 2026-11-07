/* hmap48.h: heightmap terrain on the 48K screen (the drawing is hmap48_fill.asm). */
#include <stdint.h>
#include "hmap48.h"
#include "zxgame.h"

/* hmap_line and hmap_fill are hmap48_fill.asm */

void hmap_colour(const uint8_t *h, uint8_t row0, uint8_t row1,
                 uint8_t sky, uint8_t surface, uint8_t ground, uint8_t *attrs)
{
    uint8_t c, i, r;
    for (c = 0; c < 32; c++, h += 8) {
        uint8_t lo = 255, hi = 0;
        uint8_t *a = attrs + (uint16_t)row0 * 32 + c;
        for (i = 0; i < 8; i++) {
            if (h[i] < lo) lo = h[i];
            if (h[i] > hi) hi = h[i];
        }
        for (r = row0; r <= row1; r++, a += 32) {
            uint8_t top = r * 8;
            if (top + 7 < lo) *a = sky;
            else if (top > hi) *a = ground;
            else *a = surface;
        }
    }
}
