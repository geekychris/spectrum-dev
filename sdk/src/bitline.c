/* bitline.h: polylines and the screen defaults (the lines are bitline.asm). */
#include <stdint.h>
#include "bitline.h"
#include "zxgame.h"

void bl_poly(const int8_t *xy, uint8_t n, int16_t ox, int16_t oy, uint8_t closed)
{
    int16_t x0, y0, fx, fy;
    if (!n) return;
    fx = x0 = ox + xy[0];
    fy = y0 = oy + xy[1];
    if (n == 1) {
        bl_plot(x0, y0);
        return;
    }
    for (xy += 2, n--; n; n--, xy += 2) {
        int16_t x1 = ox + xy[0], y1 = oy + xy[1];
        bl_draw_open(x0, y0, x1, y1);
        x0 = x1;
        y0 = y1;
    }
    if (closed) bl_draw_open(x0, y0, fx, fy);
    else bl_plot(x0, y0);
}

void bl_screen(void)
{
    bl_rows = zx_row;
    bl_cx0 = 0;
    bl_cx1 = 255;
    bl_cy0 = 0;
    bl_cy1 = 191;
}
