/* input_fast() (zxgame.h): input_read() decodes the keys by table, 16 lookups
   instead of a pass over every key (about 9,000 T-states less a read with
   latching on). Costs ~300 bytes, so it is a separate module: a game links it
   only by calling input_fast(). */
#include <stdint.h>
#include "zxgame.h"

typedef struct { uint8_t row, mask, bits; } keymap;     /* as input.c */
extern const keymap input_keys[];
extern const uint8_t input_nkeys;
extern uint8_t (*input_decoder)(const uint8_t *rows, uint8_t pressed_is_one);

/* the IN_* bits of the keys pressed in half-row r, from its five key bits k
   (1 = pressed): row_lo[r][k & 7] | row_hi[r][k >> 3] */
static uint8_t row_lo[8][8], row_hi[8][4];

static uint8_t decode_fast(const uint8_t *rows, uint8_t pressed_is_one)
{
    uint8_t i, k, b = 0, flip = pressed_is_one ? 0 : 0x1F;
    const uint8_t *lo = row_lo[0], *hi = row_hi[0];
    for (i = 0; i < 8; i++, lo += 8, hi += 4) {
        k = (rows[i] ^ flip) & 0x1F;
        b |= lo[k & 7] | hi[k >> 3];
    }
    return b;
}

void input_fast(void)
{
    const keymap *m;
    uint8_t n, k;
    for (m = input_keys, n = input_nkeys; n; n--, m++) {
        uint8_t *lo = row_lo[m->row], *hi = row_hi[m->row], bits = m->bits, mask = m->mask;
        for (k = 0; k < 8; k++, lo++)          /* walking pointers: zsdcc */
            if (k & mask) *lo = *lo | bits;
        for (k = 0; k < 4; k++, hi++)
            if ((k << 3) & mask) *hi = *hi | bits;
    }
    input_decoder = decode_fast;
}
