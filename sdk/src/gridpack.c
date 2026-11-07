/* Unpacks a grid of a gridpack.py set (see gridpack.h; gridpack.unpack in
   sdk/tools is the reference): a 16-bit binary arithmetic decoder. */
#include <stdint.h>
#include "gridpack.h"

static const uint8_t *bp;
static uint8_t cur, left;
static uint16_t rng, code;

static uint8_t next_bit(void)
{
    uint8_t b;
    if (left == 0) {
        cur = *bp++;
        left = 8;
    }
    left--;
    b = cur >> 7;
    cur <<= 1;
    return b;
}

/* one decision; p = P(0) in 1/256ths */
static uint8_t decode(uint8_t p)
{
    uint16_t bound = (uint16_t)(uint8_t)(rng >> 8) * p;
    uint8_t b = 0;
    if (code < bound) {
        rng = bound;
    } else {
        code -= bound;
        rng -= bound;
        b = 1;
    }
    while (rng < 0x8000) {
        rng <<= 1;
        code = (code << 1) | next_bit();
    }
    return b;
}

void gp_unpack(const gp_set *set, uint16_t index, uint8_t *out)
{
    uint8_t *o = out, *end = out + (uint16_t)set->w * set->h;
    uint8_t n = set->ntiles, w = set->w, prev = n, i;

    bp = set->bits + set->grid_off[index];
    left = 0;
    rng = 0xFFFF;
    code = 0;
    for (i = 0; i < 16; i++) code = (code << 1) | next_bit();

    while (o < end) {
        uint8_t above = (uint16_t)(o - out) >= w ? *(o - w) : n;
        uint16_t ctx = (uint16_t)prev * (n + 1) + above;
        uint8_t t;
        if (decode(set->pa[ctx])) {
            t = prev;
        } else if (above != prev && above != n && decode(set->pb[ctx])) {
            t = above;
        } else {
            /* "is it order[k]?" skipping the two ruled out; the last needs no question */
            uint8_t ruled = above != n ? above : prev, k, last = 0, s;
            const uint8_t *pl = set->pl + (uint16_t)above * n;
            t = 255;
            for (k = 0; k < n; k++) {
                s = set->order[k];
                if (s == prev || s == ruled) continue;
                last = s;
                {
                    uint8_t j, more = 0;
                    for (j = k + 1; j < n; j++) {
                        uint8_t x = set->order[j];
                        if (x != prev && x != ruled) { more = 1; break; }
                    }
                    if (!more || decode(pl[k])) { t = s; break; }
                }
            }
            if (t == 255) t = last;
        }
        *o++ = t;
        prev = t;
    }
}
