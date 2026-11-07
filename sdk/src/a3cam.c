/* a3cam.h: the amiga3d engine's camera, atan and hash, as a3d_math.c has them */
#include "a3cam.h"

static const uint16_t atan_tab[129] = {
    0, 81, 163, 244, 326, 407, 489, 570, 651, 732, 813, 894,
    975, 1056, 1136, 1217, 1297, 1377, 1457, 1537, 1617, 1696, 1775, 1854,
    1933, 2012, 2090, 2168, 2246, 2324, 2401, 2478, 2555, 2632, 2708, 2784,
    2860, 2935, 3010, 3085, 3159, 3233, 3307, 3380, 3453, 3526, 3599, 3670,
    3742, 3813, 3884, 3955, 4025, 4095, 4164, 4233, 4302, 4370, 4438, 4505,
    4572, 4639, 4705, 4771, 4836, 4901, 4966, 5030, 5094, 5157, 5220, 5282,
    5344, 5406, 5467, 5528, 5589, 5649, 5708, 5768, 5826, 5885, 5943, 6000,
    6058, 6114, 6171, 6227, 6282, 6337, 6392, 6446, 6500, 6554, 6607, 6660,
    6712, 6764, 6815, 6867, 6917, 6968, 7018, 7068, 7117, 7166, 7214, 7262,
    7310, 7358, 7405, 7451, 7498, 7544, 7589, 7635, 7679, 7724, 7768, 7812,
    7856, 7899, 7942, 7984, 8026, 8068, 8110, 8151, 8192,
};

int16_t a3_atan_ratio(int32_t num, int32_t den)
{
    int32_t a = num < 0 ? -num : num, t;
    int16_t r, f;
    if (den <= 0) return 0;
    if (a > den) a = den;
    while (den > 0x7FFFFL) { a >>= 1; den >>= 1; }      /* a * 2048 must fit */
    t = (a * 128 * 16) / den;          /* 1/16 steps of the table */
    f = (int16_t)(t & 15);
    t >>= 4;
    r = (int16_t)(atan_tab[t] + (t < 128 ? (((int16_t)(atan_tab[t + 1] - atan_tab[t])) * f) >> 4 : 0));
    return num < 0 ? -r : r;
}

uint16_t a3_atan2(int32_t x, int32_t y)
{
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int32_t a;
    if (!ax && !ay) return 0;
    if (ax <= ay) a = a3_atan_ratio(ax, ay);
    else a = 16384 - a3_atan_ratio(ay, ax);
    if (y < 0) a = 32768 - a;
    if (x < 0) a = -a;
    return (uint16_t)(a & 0xFFFF);
}

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
