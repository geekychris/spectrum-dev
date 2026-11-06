/* fixmath.h's division (the multiplies and the square root: fixmath.asm) */
#include <stdint.h>
#include "fixmath.h"

int16_t div_q14(int16_t a, int16_t d)
{
    /* (a << 14) / d, written as a product: zsdcc's 48K build got the
       shifted form wrong (-1 for -3/4) */
    int32_t n = mul16(a, 16384);
    return (int16_t)(n / d);
}
