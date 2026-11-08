/* fix16_exp.c - the amiga3d engine's fexp_neg (a3d_fix.c) on fix16.h, bit for
   bit: e^-x for x >= 0 (a series on x mod ln 2, shifted). Test: projects/fix16test. */
#include "fix16.h"

fix fexp_neg(fix x)
{
    uint8_t k;
    fix y, r, yy;
    if (x <= 0) return FONE;
    if (x > FROMI(20)) return 0;
    k = (uint8_t)(x / 45426);                           /* ln 2 */
    y = x - (fix)k * 45426;
    yy = fmul(y, y);
    r = FONE - y + yy / 2 - fmul(yy, y) / 6;
    return r >> k;
}
