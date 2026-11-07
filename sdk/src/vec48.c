/* vec48.h: rotating and scaling vertex lists (start-up work, not per frame) */
#include <stdint.h>
#include "zxgame.h"
#include "fixmath.h"
#include "vec48.h"

/* round(v * scale / 65536) for v = coordinate * 8.8 trig */
static int8_t round_q16(int32_t v)
{
    return (int8_t)((v + 0x8000L) >> 16);
}

void vec_rotate(const vec_pt *src, uint8_t n, uint8_t angle, uint16_t scale, vec_pt *dst)
{
    int16_t s = fix_sin(angle), c = fix_cos(angle);
    while (n--) {
        /* (x c - y s) fits 16 bits for |x|, |y| <= 63 only: go through 32 */
        int32_t rx = mul16(src->x, c) - mul16(src->y, s);
        int32_t ry = mul16(src->x, s) + mul16(src->y, c);
        dst->x = round_q16(rx * (int32_t)scale);
        dst->y = round_q16(ry * (int32_t)scale);
        src++;
        dst++;
    }
}

void vec_scale(const vec_pt *src, uint8_t n, uint16_t scale, vec_pt *dst)
{
    while (n--) {
        dst->x = (int8_t)((mul16(src->x, scale) + 128) >> 8);
        dst->y = (int8_t)((mul16(src->y, scale) + 128) >> 8);
        src++;
        dst++;
    }
}

void vec_rot_table(const vec_pt *src, uint8_t n, uint16_t scale, uint8_t bits, vec_pt *dst)
{
    uint16_t steps = 1 << bits, i;
    uint8_t shift = 8 - bits;
    for (i = 0; i < steps; i++) {
        vec_rotate(src, n, (uint8_t)(i << shift), scale, dst);
        dst += n;
    }
}

void vec_poly_table(const vec_pt *src, uint8_t n, uint16_t scale, uint8_t bits, uint8_t *dst)
{
    vec_pt tmp[16];
    uint16_t steps = 1 << bits, i;
    uint8_t shift = 8 - bits;
    for (i = 0; i < steps; i++) {
        vec_rotate(src, n, (uint8_t)(i << shift), scale, tmp);
        poly_compile(tmp, n, dst);
        dst += POLY_BYTES(n);
    }
}
