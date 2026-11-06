/* xorshift16 random numbers (deterministic; see zxgame.h). */
#include <stdint.h>
#include "zxgame.h"

static uint16_t rnd_state = 0xACE1;

void rnd_seed(uint16_t s)
{
    rnd_state = s ? s : 1;
}

uint16_t rnd16(void)
{
    uint16_t s = rnd_state;
    s ^= s << 7;
    s ^= s >> 9;
    s ^= s << 8;
    rnd_state = s;
    return s;
}

uint16_t rnd_range(uint16_t n)
{
    return n ? rnd16() % n : 0;
}
