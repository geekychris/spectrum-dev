/* fix16_hyp3.c - the amiga3d engine's fhypot3 (a3d_fix.c) on fix16.h, bit for
   bit: a 3-D length in 32-bit arithmetic, scaled down until the squares fit.
   Test: projects/fix16test. */
#include "fix16.h"

fix fhypot3(fix a, fix b, fix c)
{
    uint32_t x = (uint32_t)(a < 0 ? -a : a), y = (uint32_t)(b < 0 ? -b : b), z = (uint32_t)(c < 0 ? -c : c);
    uint8_t sh = 0;
    while ((x | y | z) >= 37000UL) { x >>= 1; y >>= 1; z >>= 1; sh++; }
#if defined(__SDCC)
    return (fix)(a3_isqrt(mulu16((uint16_t)x, (uint16_t)x) + mulu16((uint16_t)y, (uint16_t)y) +
                          mulu16((uint16_t)z, (uint16_t)z)) << sh);
#else
    return (fix)(a3_isqrt(x * x + y * y + z * z) << sh);
#endif
}
