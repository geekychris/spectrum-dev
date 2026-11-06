/* l2_text5(): the 5x7 font (font5.h) on the 256 x 192 Layer 2's back buffer
   (nextl2.c), one fill per run of pixels. */
#include <stdint.h>
#include "nextgfx.h"
#include "font5.h"

void l2_text5(uint8_t x, uint8_t y, const char *s, uint8_t colour, uint8_t scale)
{
    for (; *s; s++, x += (FONT5_W + 1) * scale) {
        const uint8_t *g = font5_glyph(*s);
        uint8_t r, yy = y;
        if (!g) continue;
        for (r = 0; r < FONT5_H; r++, yy += scale) {
            uint8_t bits = g[r], c = 0, xx = x;
            while (c < FONT5_W) {
                uint8_t n = 0;
                while (c + n < FONT5_W && (bits & (0x80 >> (c + n)))) n++;
                if (n) {
                    l2_fill(xx, yy, n * scale, scale, colour);
                    c += n;
                    xx += n * scale;
                }
                else {
                    c++;
                    xx += scale;
                }
            }
        }
    }
}

void l2_text5_center(uint8_t cx, uint8_t y, const char *s, uint8_t colour, uint8_t scale)
{
    l2_text5(cx - font5_width(s, scale) / 2, y, s, colour, scale);
}
