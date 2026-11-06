/* The tilemap as a text layer (see nexttile.h). */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "nexttile.h"

#define MAP  ((uint8_t *)0x4000)            /* bank 5 offset 0, in slot 2 */
#define DEFS ((uint8_t *)0x4A00)            /* offset 0x0A00 (into slot 3 past 0x6000) */

static uint8_t s2, s3;

static void map_in(void)
{
    s2 = ZXN_READ_REG(0x52);
    s3 = ZXN_READ_REG(0x53);
    ZXN_WRITE_REG(0x52, 10);
    ZXN_WRITE_REG(0x53, 11);
}

static void map_out(void)
{
    ZXN_WRITE_REG(0x52, s2);
    ZXN_WRITE_REG(0x53, s3);
}

void tm_init(void)
{
    ZXN_WRITE_REG(0x68, 0x80);              /* ULA off */
    ZXN_WRITE_REG(0x6E, 0x00);              /* map at bank 5 + 0 */
    ZXN_WRITE_REG(0x6F, 0x0A);              /* definitions at bank 5 + 0x0A00 */
    ZXN_WRITE_REG(0x6C, 0x00);              /* default attribute */
    ZXN_WRITE_REG(0x4C, 0x00);              /* pen 0 transparent */
    ZXN_WRITE_REG(0x1C, 0x08);              /* reset the tilemap clip index */
    ZXN_WRITE_REG(0x1B, 0);
    ZXN_WRITE_REG(0x1B, 159);
    ZXN_WRITE_REG(0x1B, 0);
    ZXN_WRITE_REG(0x1B, 255);
    tm_scroll(0, 0);
    tm_clear();
    ZXN_WRITE_REG(0x6B, 0x81);              /* on, 40 x 32, attributes, over the ULA */
}

void tm_scroll(uint16_t x, uint8_t y)
{
    ZXN_WRITE_REG(0x2F, (x >> 8) & 3);
    ZXN_WRITE_REG(0x30, (uint8_t)x);
    ZXN_WRITE_REG(0x31, y);
}

void tm_def(uint8_t tile, const uint8_t *defs, uint8_t n)
{
    map_in();
    memcpy(DEFS + ((uint16_t)tile << 5), defs, (uint16_t)n << 5);
    map_out();
}

void tm_font(uint8_t tile, const uint8_t *font, uint8_t n, uint8_t fg, uint8_t bg)
{
    uint8_t *d, r, k, bits;
    map_in();
    d = DEFS + ((uint16_t)tile << 5);
    for (; n; n--)
        for (r = 0; r < 8; r++) {
            bits = *font++;
            for (k = 0; k < 4; k++, bits <<= 2)
                *d++ = ((bits & 0x80) ? fg : bg) << 4 | ((bits & 0x40) ? fg : bg);
        }
    map_out();
}

void tm_glyph2x(uint8_t tile, const uint8_t *glyph, uint8_t fg, uint8_t shadow)
{
    static uint8_t px[16][16];
    uint8_t *d, x, y, t;
    memset(px, 0, sizeof(px));
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            if (glyph[y >> 1] & (0x80 >> (x >> 1))) {
                if (shadow && x < 14 && y < 14 && !px[y + 2][x + 2]) px[y + 2][x + 2] = shadow;
                px[y][x] = fg;
            }
    map_in();
    d = DEFS + ((uint16_t)tile << 5);
    for (t = 0; t < 4; t++) {
        uint8_t ox = (t & 1) << 3, oy = (t & 2) << 2;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x += 2) *d++ = px[oy + y][ox + x] << 4 | px[oy + y][ox + x + 1];
    }
    map_out();
}

void tm_put(uint8_t col, uint8_t row, uint8_t tile, uint8_t pal)
{
    uint8_t *p;
    if (col >= TM_COLS || row >= TM_ROWS) return;
    map_in();
    p = MAP + ((uint16_t)row * TM_COLS + col) * 2;
    p[0] = tile;
    p[1] = pal << 4;
    map_out();
}

void tm_print(uint8_t col, uint8_t row, const char *s, uint8_t first, uint8_t pal)
{
    uint8_t *p;
    if (row >= TM_ROWS) return;
    map_in();
    p = MAP + ((uint16_t)row * TM_COLS + col) * 2;
    for (; *s && col < TM_COLS; s++, col++) {
        uint8_t c = (uint8_t)*s;
        *p++ = (c >= 32 && c < 128) ? first + c - 32 : first;
        *p++ = pal << 4;
    }
    map_out();
}

void tm_fill(uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t tile, uint8_t pal)
{
    uint8_t *p, i;
    map_in();
    for (; h && row < TM_ROWS; h--, row++) {
        p = MAP + ((uint16_t)row * TM_COLS + col) * 2;
        for (i = 0; i < w && col + i < TM_COLS; i++) {
            *p++ = tile;
            *p++ = pal << 4;
        }
    }
    map_out();
}

void tm_clear(void)
{
    map_in();
    memset(MAP, 0, TM_COLS * TM_ROWS * 2);
    map_out();
}

void tm_pal(uint8_t pal, uint8_t pen, uint16_t rgb9)
{
    uint8_t sel = ZXN_READ_REG(0x43);
    ZXN_WRITE_REG(0x43, (sel & 0x0F) | 0x30);   /* edit the tilemap's first palette */
    ZXN_WRITE_REG(0x40, (pal << 4) | (pen & 15));
    ZXN_WRITE_REG(0x44, (uint8_t)(rgb9 >> 1));
    ZXN_WRITE_REG(0x44, rgb9 & 1);
    ZXN_WRITE_REG(0x43, sel);
}
