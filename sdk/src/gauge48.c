/* gauge48.h: a bar gauge on the 48K screen. */
#include <stdint.h>
#include "gauge48.h"
#include "zxgame.h"

/* set (fill) or clear pixels x0..x1-1 of pixel lines y..y+n-1 */
static void span(uint8_t x0, uint8_t x1, uint8_t y, uint8_t n, uint8_t fill)
{
    for (; n; n--, y++) {
        uint8_t x = x0, *row = zx_row[y];
        while (x < x1) {
            uint8_t *p = row + (x >> 3);
            if ((x & 7) == 0 && x + 8 <= x1) {
                *p = fill ? 0xFF : 0;
                x += 8;
            }
            else {
                uint8_t m = gfx_bit[x & 7];
                *p = fill ? (*p | m) : (*p & ~m);
                x++;
            }
        }
    }
}

void gauge_init(gauge *g, uint8_t col, uint8_t row, uint8_t cells, uint8_t attr)
{
    uint8_t y = row * 8, x0 = col * 8, x1 = x0 + cells * 8 - 1;
    g->col = col;
    g->row = row;
    g->cells = cells;
    g->len = 0;
    g->attr = attr;
    gfx_clear_rect(col, y, cells, 8);
    span(x0, x1 + 1, y, 1, 1);                  /* top and bottom */
    span(x0, x1 + 1, y + 7, 1, 1);
    span(x0, x0 + 1, y + 1, 6, 1);              /* ends */
    span(x1, x1 + 1, y + 1, 6, 1);
    attr_fill(col, row, cells, 1, attr);
}

void gauge_set(gauge *g, uint8_t len, uint8_t attr)
{
    uint8_t x0 = g->col * 8 + 1, y = g->row * 8 + 2;
    if (len > GAUGE_MAX(g)) len = GAUGE_MAX(g);
    if (len > g->len) span(x0 + g->len, x0 + len, y, 4, 1);
    else if (len < g->len) span(x0 + len, x0 + g->len, y, 4, 0);
    g->len = len;
    if (attr != g->attr) {
        attr_fill(g->col, g->row, g->cells, 1, attr);
        g->attr = attr;
    }
}

uint8_t gauge_len(const gauge *g, uint16_t value, uint16_t max)
{
    uint8_t len = GAUGE_MAX(g);
    if (!max) return 0;
    if (value > max) value = max;
    if (value <= 65535u / len)                  /* 16 bits: ~4x faster */
        return (uint8_t)((value * len) / max);
    return (uint8_t)(((uint32_t)value * len) / max);
}
