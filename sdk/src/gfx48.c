/* 48K screen graphics: row table, pre-shifted sprites, tiles, attributes,
   text helpers (see zxgame.h). Draws straight into the screen at 0x4000. */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "zxgame.h"
#include "zxtext.h"

uint8_t *zx_row[192];

void gfx_init(void)
{
    uint8_t y;
    for (y = 0; y < 192; y++)
        zx_row[y] = (uint8_t *)(0x4000 | ((uint16_t)(y & 0xC0) << 5) | ((uint16_t)(y & 0x07) << 8)
                                | ((uint16_t)(y & 0x38) << 2));
}

void spr_preshift(const uint8_t *src, uint8_t w, uint8_t h, uint8_t *dst)
{
    uint8_t s, y, b;
    for (s = 0; s < 8; s++) {
        for (y = 0; y < h; y++) {
            const uint8_t *line = src + y * w;
            uint8_t carry = 0;
            for (b = 0; b < w; b++) {
                *dst++ = carry | (line[b] >> s);
                carry = s ? (uint8_t)(line[b] << (8 - s)) : 0;
            }
            *dst++ = carry;
        }
    }
}

/* op: 0 xor, 1 or, 2 and-not */
static void spr_op(uint8_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h, uint8_t op)
{
    uint8_t col = x >> 3, n = w + 1, lines, i;
    const uint8_t *p = pre + (uint16_t)(x & 7) * n * h;
    if (col + n > 32) n = 32 - col;                  /* clip right */
    lines = y + h > 192 ? 192 - y : h;
    while (lines--) {
        uint8_t *d = zx_row[y++] + col;
        if (op == 0) for (i = 0; i < n; i++) d[i] ^= p[i];
        else if (op == 1) for (i = 0; i < n; i++) d[i] |= p[i];
        else for (i = 0; i < n; i++) d[i] &= ~p[i];
        p += w + 1;
    }
}

void spr_xor(uint8_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h) { spr_op(x, y, pre, w, h, 0); }
void spr_or(uint8_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h) { spr_op(x, y, pre, w, h, 1); }
void spr_clear(uint8_t x, uint8_t y, const uint8_t *pre, uint8_t w, uint8_t h) { spr_op(x, y, pre, w, h, 2); }

void gfx_fill_rect(uint8_t c0, uint8_t y, uint8_t wc, uint8_t h, uint8_t pattern)
{
    if (c0 + wc > 32) wc = 32 - c0;
    while (h-- && y < 192) memset(zx_row[y++] + c0, pattern, wc);
}

void gfx_clear_rect(uint8_t c0, uint8_t y, uint8_t wc, uint8_t h)
{
    gfx_fill_rect(c0, y, wc, h, 0);
}

void gfx_plot(uint8_t x, uint8_t y)
{
    if (y < 192) zx_row[y][x >> 3] |= 0x80 >> (x & 7);
}

void gfx_unplot(uint8_t x, uint8_t y)
{
    if (y < 192) zx_row[y][x >> 3] &= ~(0x80 >> (x & 7));
}

uint8_t gfx_point(uint8_t x, uint8_t y)
{
    return y < 192 && (zx_row[y][x >> 3] & (0x80 >> (x & 7))) ? 1 : 0;
}

void gfx_hline(uint8_t x0, uint8_t x1, uint8_t y)
{
    for (;; x0++) {
        gfx_plot(x0, y);
        if (x0 == x1) break;
    }
}

void tile_draw(uint8_t col, uint8_t row, const uint8_t *tile)
{
    uint8_t *d = zx_cxy2saddr(col, row), i;
    for (i = 0; i < 8; i++) {
        *d = tile[i];
        d += 256;
    }
}

void attr_set(uint8_t col, uint8_t row, uint8_t attr)
{
    *zx_cxy2aaddr(col, row) = attr;
}

void attr_fill(uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t attr)
{
    while (h--) memset(zx_cxy2aaddr(col, row++), attr, w);
}

void screen_clear(uint8_t attr, uint8_t border)
{
    memset((void *)0x4000, 0, 6144);
    memset((void *)0x5800, attr, 768);
    snd_border = border & 7;
    zx_border(snd_border);
}

/* each ROM glyph pixel becomes 2x2: a byte -> two bytes, each line twice */
static const uint8_t widen4[16] = {
    0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
    0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
};

void text_big(uint8_t col, uint8_t row, const char *s)
{
    while (*s && col < 31) {
        const uint8_t *g = (const uint8_t *)0x3C00 + (uint16_t)(uint8_t)*s * 8;
        uint8_t i, y = row * 8;
        for (i = 0; i < 8; i++) {
            uint8_t hi = widen4[g[i] >> 4], lo = widen4[g[i] & 15];
            uint8_t *d = zx_row[y] + col;
            d[0] = hi; d[1] = lo;
            d = zx_row[y + 1] + col;
            d[0] = hi; d[1] = lo;
            y += 2;
        }
        s++;
        col += 2;
    }
}

void text_num(uint8_t col, uint8_t row, uint32_t v, uint8_t digits)
{
    char buf[11];
    uint8_t i = digits > 10 ? 10 : digits;
    buf[i] = 0;
    while (i--) {
        buf[i] = '0' + (uint8_t)(v % 10);
        v /= 10;
    }
    zx_print_at(col, row, buf);
}

void text_center(uint8_t row, const char *s)
{
    uint8_t n = strlen(s);
    zx_print_at(n >= 32 ? 0 : (32 - n) / 2, row, s);
}
