/* a3cam.h: the amiga3d engine's camera and hash, as a3d_math.c has them
   (its arc tangents are fix16.h's, fix16_atan.c) */
#include "a3cam.h"

uint32_t a3_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

void cam_view(A3Cam *c, int16_t vw, int16_t vh, int16_t cx, int16_t cy, int16_t fov)
{
    c->vw = vw;
    c->vh = vh;
    c->cx = cx;
    c->cyc = cy;
    c->fov = fov;
}

void cam_setup(A3Cam *c)
{
    c->cy = a3_cos(c->yaw);
    c->sy = a3_sin(c->yaw);
    c->cp = a3_cos(c->pitch);
    c->sp = a3_sin(c->pitch);
}

void cam_xform(const A3Cam *c, int32_t wx, int32_t wy, int32_t wz, A3V3 *out)
{
    int32_t dx = wx - c->x, dz = wz - c->z, dy = wy - c->y, rx, rz;
    int32_t cy = c->cy >> 4, sy = c->sy >> 4, cp = c->cp >> 4, sp = c->sp >> 4;
    rx = (dx * cy - dz * sy) >> 12;
    rz = (dx * sy + dz * cy) >> 12;
    out->x = rx;
    out->y = (dy * cp + rz * sp) >> 12;
    out->z = (rz * cp - dy * sp) >> 12;
}

void cam_project(const A3Cam *c, const A3V3 *p, int32_t *sx, int32_t *sy)
{
    *sx = c->cx + (p->x * c->fov) / p->z;
    *sy = c->cyc - (p->y * c->fov) / p->z;
}

int16_t cam_horizon(const A3Cam *c)
{
    return (int16_t)(c->cyc - (int32_t)(((int32_t)c->fov * (c->sp >> 4)) / (c->cp >> 4)));
}
