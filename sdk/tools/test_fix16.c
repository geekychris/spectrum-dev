/* fix16.c's Z80 code (fmul, fdiv, a3_angle from 16 x 16 multiplies),
   compiled on the host with -DFIX16_Z80_C, against the amiga3d engine's
   64-bit definitions; run by test_tools.py:
     cc -DFIX16_Z80_C -I../include test_fix16.c ../src/fix16.c ../src/fix16_tab.c (fix16.c includes fix16_model.h, the asm's model) */
#include <stdio.h>
#include "fix16.h"

uint32_t mulu16(uint16_t a, uint16_t b) { return (uint32_t)a * b; }

static uint32_t s = 12345;
static int32_t rnd(void)
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    switch (s & 3) {                    /* small, medium, large and edge values */
    case 0: return (int32_t)(s >> 8) - (1 << 23);
    case 1: return (int32_t)(s >> 15) - (1 << 16);
    case 2: return (int32_t)s;
    }
    return (s & 4) ? (int32_t)0x80000000u + (int32_t)((s >> 3) & 3) : 0x7FFFFFFF - (int32_t)((s >> 3) & 3);
}

int main(void)
{
    long n, bad = 0;
    for (n = 0; n < 2000000; n++) {
        int32_t a = rnd(), b = rnd();
        int64_t q;
        if ((n & 15) == 0) b = (n & 16) ? 65536 : -65536;       /* fmul's shortcuts: exactly 1.0, 0 */
        if ((n & 31) == 1) a = (n & 64) ? 65536 : (n & 32) ? 0 : -65536;
        if (fmul(a, b) != (fix)(((int64_t)a * b) >> 16)) { if (bad++ < 5) printf("fmul %ld %ld\n", (long)a, (long)b); }
        if (b != 0) {
            q = ((int64_t)a * 65536) / b;
            if (fdiv(a, b) != (fix)q) { if (bad++ < 5) printf("fdiv %ld %ld\n", (long)a, (long)b); }
        }
        if (a3_angle(a) != (int16_t)(((int64_t)a * 683565276LL) >> 32)) { if (bad++ < 5) printf("angle %ld\n", (long)a); }
    }
    printf("fix16: %ld mismatches\n", bad);
    return bad != 0;
}
