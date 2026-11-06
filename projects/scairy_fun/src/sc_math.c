/*
 * sc_math.c - the shared engine's (amiga3d/a3d_math.c) functions that the
 * simulation uses, unchanged: the LCG, the hash and atan of a ratio.
 */
#include <stdint.h>
#include "sc.h"

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

int a3_atan_ratio(int32_t num, int32_t den)
{
    int32_t a = num < 0 ? -num : num, t, f;
    int r;
    if (den <= 0) return 0;
    if (a > den) a = den;
    while (den > 0x7FFFF) { a >>= 1; den >>= 1; }      /* a * 2048 must fit */
    t = (a * 128 * 16) / den;          /* 1/16 steps of the table */
    f = t & 15;
    t >>= 4;
    r = atan_tab[t] + (t < 128 ? ((atan_tab[t + 1] - atan_tab[t]) * f) >> 4 : 0);
    return num < 0 ? -r : r;
}

uint32_t a3_rand(uint32_t *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (*state >> 16) & 0x7FFF;
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
