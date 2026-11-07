/* vec3.h: v3_norm32 (both), and on a host compiler every function in C
   (the Z80's are vec3.asm) */
#include <stdint.h>
#include "vec3.h"
#include "fixmath.h"

/* the 32-bit part of v3_norm32: halve until the components fit 16 bits
   (v3_norm's first loop would do the same), then v3_norm */
void v3_norm32(vec3 *r, int32_t x, int32_t y, int32_t z)
{
    vec3 a;
    while (x > 30000 || x < -30000 || y > 30000 || y < -30000 || z > 30000 || z < -30000) {
        x >>= 1;
        y >>= 1;
        z >>= 1;
    }
    a.x = (int16_t)x;
    a.y = (int16_t)y;
    a.z = (int16_t)z;
    v3_norm(r, &a);
}

#ifndef __SDCC
int32_t v3_dot32(const vec3 *a, const vec3 *b)
{
    return (int32_t)a->x * b->x + (int32_t)a->y * b->y + (int32_t)a->z * b->z;
}

int16_t v3_dot(const vec3 *a, const vec3 *b) { return (int16_t)(v3_dot32(a, b) >> 14); }

void v3_cross(vec3 *r, const vec3 *a, const vec3 *b)
{
    vec3 t;
    t.x = (int16_t)(((int32_t)a->y * b->z - (int32_t)a->z * b->y) >> 14);
    t.y = (int16_t)(((int32_t)a->z * b->x - (int32_t)a->x * b->z) >> 14);
    t.z = (int16_t)(((int32_t)a->x * b->y - (int32_t)a->y * b->x) >> 14);
    *r = t;
}

void v3_scale(vec3 *r, const vec3 *a, int16_t s)
{
    r->x = (int16_t)(((int32_t)a->x * s) >> 14);
    r->y = (int16_t)(((int32_t)a->y * s) >> 14);
    r->z = (int16_t)(((int32_t)a->z * s) >> 14);
}

void v3_mat(vec3 *r, const vec3 *m, const vec3 *a)
{
    vec3 t;
    t.x = v3_dot(&m[0], a);
    t.y = v3_dot(&m[1], a);
    t.z = v3_dot(&m[2], a);
    *r = t;
}

void v3_norm(vec3 *r, const vec3 *v)
{
    int32_t x = v->x, y = v->y, z = v->z, inv;
    uint32_t len;
    int sh = 0;
    while (x > 30000 || x < -30000 || y > 30000 || y < -30000 || z > 30000 || z < -30000) {
        x >>= 1; y >>= 1; z >>= 1;
    }
    while (x < 4096 && x > -4096 && y < 4096 && y > -4096 && z < 4096 && z > -4096 &&
           (x | y | z) != 0 && sh > -8) {
        x <<= 1; y <<= 1; z <<= 1; sh--;
    }
    len = isqrt32_fast((uint32_t)(x * x) + (uint32_t)(y * y) + (uint32_t)(z * z));
    if (len == 0) { r->x = 0; r->y = V3_ONE; r->z = 0; return; }
    inv = (int32_t)((1L << 28) / (int32_t)len);
    r->x = (int16_t)((x * inv) >> 14);
    r->y = (int16_t)((y * inv) >> 14);
    r->z = (int16_t)((z * inv) >> 14);
}
#endif
