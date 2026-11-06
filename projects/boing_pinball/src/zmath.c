/* Boing Pinball's own maths: the Q14 sine of an angle in 1/4096 radian
   and the original's random numbers (the multiplies are the SDK's
   fixmath.h). */
#include <stdint.h>
#include "pbz.h"

extern const int16_t sin_quarter[257];

/* A (1/4096 rad) -> Q14: 1024 steps a turn from the quarter table, interpolated */
int16_t sin_q14(int16_t a)
{
    int32_t u = mul16(a, 2608);                 /* 1024 * 65536 / (2 pi 4096) */
    uint16_t i = (uint16_t)(u >> 16) & 1023, f = (uint16_t)(u >> 8) & 255;
    uint16_t q = i & 255, quad = i >> 8;
    int16_t s0, s1;
    if (quad & 1) { s0 = sin_quarter[256 - q]; s1 = sin_quarter[255 - q]; }
    else { s0 = sin_quarter[q]; s1 = sin_quarter[q + 1]; }
    s0 += (int16_t)(((int32_t)(s1 - s0) * f) >> 8);
    return quad & 2 ? -s0 : s0;
}

uint32_t a3_rand(uint32_t *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (*state >> 16) & 0x7FFF;
}
