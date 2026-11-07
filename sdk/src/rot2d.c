/* fixmath.h rot2d_set: the coefficients for rot2d (rot2d.asm). */
#include <stdint.h>
#include "fixmath.h"

extern int16_t rot2d_kcx, rot2d_ksx, rot2d_kcy, rot2d_ksy;

void rot2d_set(uint8_t angle, int16_t scale_x, int16_t scale_y)
{
    int16_t c = fix_cos14(angle), s = fix_sin14(angle);
    rot2d_kcx = mulq14(c, scale_x);
    rot2d_ksx = mulq14(s, scale_x);
    rot2d_kcy = mulq14(c, scale_y);
    rot2d_ksy = mulq14(s, scale_y);
}
