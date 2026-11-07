/* Sprite groups with zoom and a palette offset, and hiding a range of
   sprites (see nextspr.h; in a file of their own, so games that don't
   call them don't link them). */
#include <stdint.h>
#include <z80.h>
#include "nextspr.h"

static uint8_t attr[5 * 16];            /* a group's attributes, sent with one OTIR */

uint8_t sprg_show_ex(uint8_t first, int16_t x, int16_t y, uint8_t cols, uint8_t rows,
                     uint8_t slot, uint8_t flags, uint8_t pal, uint8_t zoom)
{
    uint8_t r, c, *a = attr, n = 0, step = 16 << zoom;
    uint8_t a2 = (pal << 4) | (flags & 0x0E), a4 = (zoom << 3) | (zoom << 1);
    int16_t sx, sy = y;
    z80_outp(0x303B, first & 127);
    for (r = 0; r < rows; r++, sy += step) {
        sx = x;
        for (c = 0; c < cols && n < 16; c++, sx += step, slot++, n++, a += 5) {
            if (sx <= -step || sx >= 320 || sy <= -step || sy >= 256) {
                a[0] = a[1] = a[2] = a[4] = 0;
                a[3] = 0x40;                                         /* hidden, 5 bytes */
            } else {
                a[0] = (uint8_t)sx;
                a[1] = (uint8_t)sy;
                a[2] = a2 | ((sx >> 8) & 1);
                a[3] = 0xC0 | (slot & 63);                           /* visible, 5 bytes */
                a[4] = a4 | ((sy >> 8) & 1);                         /* 8 bpp, zoom XX YY */
            }
        }
    }
    z80_otir(attr, 0x57, n * 5);
    return first + n;
}

static uint8_t zero[64];                /* 16 hidden sprites' attributes (4 bytes each) */

void sprg_hide(uint8_t first, uint8_t n)
{
    if (first >= 128) return;
    if (n > 128 - first) n = 128 - first;
    z80_outp(0x303B, first);
    for (; n > 16; n -= 16) z80_otir(zero, 0x57, 64);
    if (n) z80_otir(zero, 0x57, n << 2);
}
