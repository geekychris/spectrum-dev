/*
 * fix16_atan.c - the amiga3d engine's arc tangents (a3d_math.c a3_atan_ratio,
 * a3_atan2; a3d_fix.c fatan2, fasin) on fix16.h, bit for bit. Lander 3D's
 * rules (tilt, the autopilot's attitude); test: projects/fix16test.
 */
#include "fix16.h"

/* atan(t / 128) for t = 0..128, in 1/65536 turn */
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
    uint8_t f, i;
    int16_t r;
    if (den <= 0) return 0;
    if (a > den) a = den;
    while (den > 0x7FFFFL) { a >>= 1; den >>= 1; }      /* a * 2048 must fit */
    t = (a * 2048) / den;                               /* 1/16 steps of the table */
    f = (uint8_t)t & 15;
    i = (uint8_t)(t >> 4);
    r = (int16_t)atan_tab[i];
    if (i < 128) r += (int16_t)(((int16_t)(atan_tab[i + 1] - atan_tab[i]) * f) >> 4);
    return num < 0 ? -r : r;
}

uint16_t a3_atan2(int32_t x, int32_t y)
{
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int16_t a;
    if (!ax && !ay) return 0;
    if (ax <= ay) a = a3_atan_ratio(ax, ay);
    else a = 16384 - a3_atan_ratio(ay, ax);
    if (y < 0) a = 32768 - a;
    if (x < 0) a = -a;
    return (uint16_t)a;
}

fix fatan2(fix y, fix x)
{
    fix ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int32_t t;                                          /* -32768..32768 */
    if (!ax && !ay) return 0;
    if (ay <= ax) t = a3_atan_ratio(ay, ax);
    else t = 16384 - a3_atan_ratio(ax, ay);
    if (x < 0) t = 32768 - t;
    if (y < 0) t = -t;
    return fmul(t, 411775L);                            /* 16-bit angle -> radians: (t * 411775) >> 16 */
}

fix fasin(fix x) { return fatan2(x, fsqrt(FONE - fmul(x, x))); }
