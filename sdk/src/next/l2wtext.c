/* ROM-font text on Layer 2's 320 x 256 mode (see nextgfx.h, l2w_text). */
#include <stdint.h>
#include "nextgfx.h"

/* ROM font (0x3D00: the 48K BASIC ROM must be paged in at 0x0000). Narrow:
   columns 1..6 of each glyph only (the ROM's glyphs leave 0 and 7 blank) */
uint16_t l2w_text(uint16_t x, uint8_t y, const char *s, uint8_t colour, uint8_t narrow)
{
    uint8_t first = narrow ? 1 : 0, last = narrow ? 6 : 7;
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s, b, r;
        const uint8_t *g = (const uint8_t *)0x3C00 + (uint16_t)(c < 32 || c > 127 ? '?' : c) * 8;
        for (b = first; b <= last; b++, x++) {
            uint8_t m = 0x80 >> b, *p;
            if (x >= 320) continue;
            p = l2w_column(x) + y;
            for (r = 0; r < 8; r++, p++)
                if (g[r] & m) *p = colour;
        }
    }
    l2w_done();
    return x;
}
