/* ap_a3d.c - port: amiga3d's a3_atan2, a3_hypot and a3_rand (a3d_math.c),
   the same results (the rest of the engine's maths is the SDK's fix16) */
#include <stdint.h>
#include "a3d.h"

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

/* a3_atan_ratio for magnitudes num <= ... (a3_atan2 passes |x|, |y|) */
static uint16_t atan_ratio(uint32_t a, uint32_t den)
{
    uint32_t t;
    uint8_t f;
    if (!den) return 0;
    if (a > den) a = den;
    while (den > 0x7FFFF) { a >>= 1; den >>= 1; }      /* a * 2048 must fit */
    t = (a << 11) / den;                                /* 1/16 steps of the table */
    f = (uint8_t)t & 15;
    t >>= 4;
    return atan_tab[t] + (t < 128 ? (uint16_t)(((atan_tab[t + 1] - atan_tab[t]) * f) >> 4) : 0);
}

int a3_atan2(int32_t x, int32_t y)
{
    uint32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    uint16_t a;
    if (!ax && !ay) return 0;
    if (ax <= ay) a = atan_ratio(ax, ay);
    else a = 16384 - atan_ratio(ay, ax);
    if (y < 0) a = 32768 - a;
    if (x < 0) a = -a;
    return a;                           /* port: 0..65535 as int: the same modulo 65536 */
}

int32_t a3_hypot(int32_t x, int32_t y)
{
    uint32_t ax = (uint32_t)(x < 0 ? -x : x), ay = (uint32_t)(y < 0 ? -y : y);
    uint8_t sh = 0;
    while ((ax | ay) >= 46000) { ax >>= 1; ay >>= 1; sh++; }
    return (int32_t)(a3_isqrt(ax * ax + ay * ay) << sh);
}

uint32_t a3_rand(uint32_t *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (*state >> 16) & 0x7FFF;
}

/* a3d_fix.c fhypot3: lengths in 32-bit arithmetic, scaled down until the
   squares fit */
fix fhypot3(fix a, fix b, fix c)
{
    uint32_t x = (uint32_t)(a < 0 ? -a : a), y = (uint32_t)(b < 0 ? -b : b), z = (uint32_t)(c < 0 ? -c : c);
    uint8_t sh = 0;
    while ((x | y | z) >= 37000) { x >>= 1; y >>= 1; z >>= 1; sh++; }
    return (fix)(a3_isqrt(x * x + y * y + z * z) << sh);
}
