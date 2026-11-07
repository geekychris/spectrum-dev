/* l2line.h: clipping (Cohen-Sutherland) and the call into l2line.asm */
#include <stdint.h>
#include "l2line.h"

extern uint8_t l2l_x0, l2l_y0, l2l_x1, l2l_y1, l2l_col;
extern void l2l_core(void);

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

void l2l_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t colour)
{
    uint8_t c0 = outcode(x0, y0), c1 = outcode(x1, y1);
    while (c0 | c1) {
        uint8_t c;
        int16_t x, y;
        if (c0 & c1) return;
        c = c0 ? c0 : c1;
        if (c & C_TOP) { x = x0 + (int16_t)((int32_t)(x1 - x0) * (0 - y0) / (y1 - y0)); y = 0; }
        else if (c & C_BOTTOM) { x = x0 + (int16_t)((int32_t)(x1 - x0) * (191 - y0) / (y1 - y0)); y = 191; }
        else if (c & C_LEFT) { y = y0 + (int16_t)((int32_t)(y1 - y0) * (0 - x0) / (x1 - x0)); x = 0; }
        else { y = y0 + (int16_t)((int32_t)(y1 - y0) * (255 - x0) / (x1 - x0)); x = 255; }
        if (c == c0) { x0 = x; y0 = y; c0 = outcode(x0, y0); }
        else { x1 = x; y1 = y; c1 = outcode(x1, y1); }
    }
    l2l_x0 = (uint8_t)x0;
    l2l_y0 = (uint8_t)y0;
    l2l_x1 = (uint8_t)x1;
    l2l_y1 = (uint8_t)y1;
    l2l_col = colour;
    l2l_core();
}
