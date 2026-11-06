/* Isometric draw order (see isosort.h). Builds on the host too (tests). */
#include <stdint.h>
#include <string.h>
#include "isosort.h"

void iso_set(IsoBox *b, int16_t x0, int16_t y0, int16_t z0, int16_t x1, int16_t y1,
             int16_t z1, int16_t sx0, int16_t sy0, int16_t sx1, int16_t sy1)
{
    b->x0 = x0; b->y0 = y0; b->z0 = z0;
    b->x1 = x1; b->y1 = y1; b->z1 = z1;
    b->sx0 = sx0; b->sy0 = sy0; b->sx1 = sx1; b->sy1 = sy1;
    b->key = ((int32_t)x0 + x1 + z0 + z1) * 17 - ((int32_t)y0 + y1) * 14;
}

uint8_t iso_rects_meet(const IsoBox *a, const IsoBox *b)
{
    return a->sx0 <= b->sx1 && b->sx0 <= a->sx1 && a->sy0 <= b->sy1 && b->sy0 <= a->sy1;
}

int8_t iso_order(const IsoBox *a, const IsoBox *b, int16_t e)
{
    uint8_t ab = a->x0 >= b->x1 - e || a->z0 >= b->z1 - e || a->y1 <= b->y0 + e;
    uint8_t ba = b->x0 >= a->x1 - e || b->z0 >= a->z1 - e || b->y1 <= a->y0 + e;
    if (ab && !ba) return 1;
    if (ba && !ab) return -1;
    if (a->key != b->key) return a->key > b->key ? 1 : -1;
    return 0;
}

uint8_t iso_behind(const IsoBox *a, const IsoBox *b, int16_t eps)
{
    return iso_rects_meet(a, b) && iso_order(a, b, eps) > 0;
}

/* before[j] bit i: box i must be drawn before box j */
static uint8_t before[ISO_MAX][(ISO_MAX + 7) / 8];
static uint8_t mark[ISO_MAX];
static const IsoBox *bx;
static uint8_t nb, *out, nout;

static void visit(uint8_t j)
{
    uint8_t i;
    mark[j] = 1;
    for (i = 0; i < nb; i++)
        if ((before[j][i >> 3] & (1 << (i & 7))) && !mark[i]) visit(i);
    out[nout++] = j;
}

void iso_sort(const IsoBox *boxes, uint8_t n, uint8_t *order, int16_t eps)
{
    uint8_t i, j;
    int8_t o;
    if (n > ISO_MAX) n = ISO_MAX;
    bx = boxes;
    nb = n;
    out = order;
    nout = 0;
    memset(before, 0, sizeof(before));
    memset(mark, 0, sizeof(mark));
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++) {
            if (!iso_rects_meet(&boxes[i], &boxes[j])) continue;
            o = iso_order(&boxes[i], &boxes[j], eps);
            if (o > 0) before[j][i >> 3] |= (uint8_t)(1 << (i & 7));
            else if (o < 0) before[i][j >> 3] |= (uint8_t)(1 << (j & 7));
        }
    for (i = 0; i < n; i++)
        if (!mark[i]) visit(i);
}
