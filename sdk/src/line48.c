/* line48.h: row tables, C entry points and clipping (the drawing is line48.asm) */
#include <stdint.h>
#include "line48.h"

extern uint8_t line_x0, line_y0, line_x1, line_y1, line_skip, line_signed;
extern uint8_t lp_cx, lp_cy, lp_n, lp_closed, pl_n;
extern const vec_pt *lp_pts;
extern const uint8_t *pl_ptr;
extern void line_core(void);
extern void lp_core(void);
extern void pl_core(void);

#define ROW_LO ((uint8_t *)0xF200)
#define ROW_HI ((uint8_t *)0xF300)

void line48_init(void)
{
    uint16_t y;
    for (y = 0; y < 256; y++) {
        uint8_t r = y < 192 ? y : (y < 224 ? y - 192 : y - 64);
        uint16_t a = 0x4000 | ((uint16_t)(r & 0xC0) << 5) | ((uint16_t)(r & 0x07) << 8) |
                     ((uint16_t)(r & 0x38) << 2);
        ROW_LO[y] = (uint8_t)a;
        ROW_HI[y] = (uint8_t)(a >> 8);
    }
}

void line_draw(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    line_x0 = x0;
    line_y0 = y0;
    line_x1 = x1;
    line_y1 = y1;
    line_skip = 0;
    line_signed = 0;
    line_core();
}

/* Cohen-Sutherland against 0..255 x 0..191 */
#define C_LEFT 1
#define C_RIGHT 2
#define C_TOP 4
#define C_BOTTOM 8

static uint8_t outcode(int16_t x, int16_t y)
{
    uint8_t c = 0;
    if (x < 0) c = C_LEFT;
    else if (x > 255) c = C_RIGHT;
    if (y < 0) c |= C_TOP;
    else if (y > 191) c |= C_BOTTOM;
    return c;
}

uint8_t line_clip(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    uint8_t c0 = outcode(x0, y0), c1 = outcode(x1, y1);
    while (c0 | c1) {
        uint8_t c;
        int16_t x, y;
        if (c0 & c1) return 0;
        c = c0 ? c0 : c1;
        if (c & C_TOP) {
            x = x0 + (int16_t)((int32_t)(x1 - x0) * (0 - y0) / (y1 - y0));
            y = 0;
        } else if (c & C_BOTTOM) {
            x = x0 + (int16_t)((int32_t)(x1 - x0) * (191 - y0) / (y1 - y0));
            y = 191;
        } else if (c & C_LEFT) {
            y = y0 + (int16_t)((int32_t)(y1 - y0) * (0 - x0) / (x1 - x0));
            x = 0;
        } else {
            y = y0 + (int16_t)((int32_t)(y1 - y0) * (255 - x0) / (x1 - x0));
            x = 255;
        }
        if (c == c0) { x0 = x; y0 = y; c0 = outcode(x0, y0); }
        else { x1 = x; y1 = y; c1 = outcode(x1, y1); }
    }
    line_draw((uint8_t)x0, (uint8_t)y0, (uint8_t)x1, (uint8_t)y1);
    return 1;
}

void line_poly(uint8_t cx, uint8_t cy, const vec_pt *pts, uint8_t n, uint8_t closed)
{
    if (!n) return;
    lp_cx = cx;
    lp_cy = cy;
    lp_pts = pts;
    lp_n = n;
    lp_closed = closed;
    lp_core();
}

void plot_list(const uint8_t *xy, uint8_t n)
{
    pl_ptr = xy;
    pl_n = n;
    pl_core();
}
