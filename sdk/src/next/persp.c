/* persp.h: the scale and the point loop (persp_mul is persp.asm on the Z80) */
#include <stdint.h>
#include "persp.h"

uint32_t persp_k = 144UL << 12;

void persp_fov(int16_t fov) { persp_k = (uint32_t)fov << 12; }

#ifndef __SDCC
uint16_t persp_scale(uint16_t z)        /* (persp.asm on the Z80) */
{
    uint32_t s;
    if (z < 1) z = 1;
    s = persp_k / z;
    return s > 65535UL ? 65535 : (uint16_t)s;
}

int16_t persp_x1(int16_t x, int16_t bx, uint16_t s, int16_t cx)
{
    int16_t out;
    persp_xs(&x, 1, bx, s, cx, &out);
    return out;
}

int16_t persp_y1(int16_t y, int16_t by, uint16_t s, int16_t cy)
{
    int32_t v = (int32_t)y + by;
    int32_t r = (int32_t)cy - persp_mul((int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v), s);
    return (int16_t)(r > PERSP_CLAMP ? PERSP_CLAMP : r < -PERSP_CLAMP ? -PERSP_CLAMP : r);
}
#endif

#ifndef __SDCC
static int16_t clamp(int16_t c, int16_t v)
{
    int32_t r = (int32_t)c + v;
    if (r > PERSP_CLAMP) return PERSP_CLAMP;
    if (r < -PERSP_CLAMP) return -PERSP_CLAMP;
    return (int16_t)r;
}

/* (persp.asm on the Z80) */
void persp_xy(const int16_t *pts, uint8_t n, int16_t bx, int16_t by, uint16_t s,
              int16_t cx, int16_t cy, int16_t *out)
{
    while (n--) {
        int32_t x = (int32_t)pts[0] + bx, y = (int32_t)pts[1] + by;
        if (x > 32767) x = 32767;
        if (x < -32767) x = -32767;
        if (y > 32767) y = 32767;
        if (y < -32767) y = -32767;
        out[0] = clamp(cx, persp_mul((int16_t)x, s));
        out[1] = clamp(cy, -persp_mul((int16_t)y, s));
        pts += 2;
        out += 2;
    }
}

void persp_xs(const int16_t *xs, uint8_t n, int16_t bx, uint16_t s, int16_t cx, int16_t *out)
{
    while (n--) {
        int32_t x = (int32_t)*xs++ + bx;
        if (x > 32767) x = 32767;
        if (x < -32767) x = -32767;
        *out++ = clamp(cx, persp_mul((int16_t)x, s));
    }
}
#endif
