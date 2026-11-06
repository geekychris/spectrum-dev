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

static uint8_t attr[5];

uint8_t sprg_show(uint8_t first, int16_t x, int16_t y, uint8_t cols, uint8_t rows,
                  uint8_t slot, uint8_t flags)
{
    uint8_t r, c;
    int16_t sx, sy = y;
    z80_outp(0x303B, first & 127);
    for (r = 0; r < rows; r++, sy += 16) {
        sx = x;
        for (c = 0; c < cols; c++, sx += 16, slot++, first++) {
            if (sx <= -16 || sx >= 320 || sy <= -16 || sy >= 256) {
                attr[0] = attr[1] = attr[2] = attr[4] = 0;
                attr[3] = 0x40;                                            /* hidden, 5 bytes */
            } else {
                attr[0] = (uint8_t)sx;
                attr[1] = (uint8_t)sy;
                attr[2] = (flags & 0x0E) | ((sx >> 8) & 1);
                attr[3] = 0xC0 | (slot & 63);                              /* visible, 5 bytes */
                attr[4] = (sy >> 8) & 1;
            }
            z80_otir(attr, 0x57, 5);
        }
    }
    return first;
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

/* the byte of the image for pixel (x, y) */
static uint8_t *at(uint8_t *img, uint8_t cols, uint8_t x, uint8_t y)
{
    return img + (((uint16_t)((y >> 4) * cols + (x >> 4))) << 8) + ((y & 15) << 4) + (x & 15);
}

void sprg_mask_rle(uint8_t *img, uint8_t cols, uint8_t rows, int16_t px, int16_t py,
                   const uint8_t *piece)
{
    int16_t gw = cols * 16, gh = rows * 16, x, y, y0, y1;
    uint8_t w = piece[0], skip, len;
    const uint8_t *p = piece + 2;
    uint16_t rowstep = (uint16_t)(cols - 1) << 8;       /* from row 15 of a pattern to row 0 below */
    for (x = px; w; w--, x++) {
        y = py;
        for (;;) {
            skip = *p++;
            if (skip == 0xFF) break;
            len = *p++;
            y += skip;
            if (x >= 0 && x < gw) {
                y0 = y < 0 ? 0 : y;
                y1 = y + len > gh ? gh : y + len;
                if (y0 < y1) {
                    uint8_t *q = at(img, cols, (uint8_t)x, (uint8_t)y0);
                    uint8_t n = (uint8_t)(y1 - y0), r = (uint8_t)y0 & 15;
                    for (; n; n--) {
                        *q = SPRG_TRANSPARENT;
                        q += 16;
                        if (++r == 16) { r = 0; q += rowstep; }
                    }
                }
            }
            p += len;
            y += len;
        }
    }
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
