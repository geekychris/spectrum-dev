/* Sprite groups (see nextspr.h). */
#include <stdint.h>
#include <z80.h>
#include "nextgfx.h"
#include "nextspr.h"

void sprg_upload(uint8_t slot, const uint8_t *img, uint8_t n)
{
    z80_outp(0x303B, slot & 63);
    dma_to_port(0x5B, img, (uint16_t)n << 8);
}

static uint8_t attr[5 * 16];            /* a group's attributes, sent with one OTIR */

uint8_t sprg_show(uint8_t first, int16_t x, int16_t y, uint8_t cols, uint8_t rows,
                  uint8_t slot, uint8_t flags)
{
    uint8_t r, c, *a = attr, n = 0;
    int16_t sx, sy = y;
    z80_outp(0x303B, first & 127);
    for (r = 0; r < rows; r++, sy += 16) {
        sx = x;
        for (c = 0; c < cols && n < 16; c++, sx += 16, slot++, n++, a += 5) {
            if (sx <= -16 || sx >= 320 || sy <= -16 || sy >= 256) {
                a[0] = a[1] = a[2] = a[4] = 0;
                a[3] = 0x40;                                         /* hidden, 5 bytes */
            } else {
                a[0] = (uint8_t)sx;
                a[1] = (uint8_t)sy;
                a[2] = (flags & 0x0E) | ((sx >> 8) & 1);
                a[3] = 0xC0 | (slot & 63);                           /* visible, 5 bytes */
                a[4] = (sy >> 8) & 1;
            }
        }
    }
    z80_otir(attr, 0x57, n * 5);
    return first + n;
}

void sprg_hide_from(uint8_t n)
{
    static uint8_t zero[4] = { 0, 0, 0, 0 };
    if (n >= 128) return;
    z80_outp(0x303B, n);
    do {
        z80_otir(zero, 0x57, 4);
    } while (++n < 128);
}

/* nextspr_fill.asm: the mask, in asm (it is the hot loop of an isometric game) */
extern void sprg_mask_core(uint8_t *img, uint16_t cols, uint16_t rows, int16_t px, int16_t py,
                           const uint8_t *piece);

/* the byte of the image for pixel (x, y) */
static uint8_t *at(uint8_t *img, uint8_t cols, uint8_t x, uint8_t y)
{
    return img + (((uint16_t)((y >> 4) * cols + (x >> 4))) << 8) + ((y & 15) << 4) + (x & 15);
}

void sprg_mask_rle(uint8_t *img, uint8_t cols, uint8_t rows, int16_t px, int16_t py,
                   const uint8_t *piece)
{
    if (px >= cols * 16 || px + piece[0] <= 0 || py >= rows * 16 || py + piece[1] <= 0) return;
    sprg_mask_core(img, cols, rows, px, py, piece);
}

void sprg_mask_rect(uint8_t *img, uint8_t cols, uint8_t rows, int16_t x0, int16_t y0,
                    int16_t x1, int16_t y1)
{
    int16_t gw = cols * 16, gh = rows * 16, x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > gw) x1 = gw;
    if (y1 > gh) y1 = gh;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) *at(img, cols, (uint8_t)x, (uint8_t)y) = SPRG_TRANSPARENT;
}
