/* colring.h: a ring of columns over a wide world. */
#include <stdint.h>
#include "colring.h"

void cr_init(colring *r, uint16_t n, uint32_t wrap, cr_fill_fn fill, void *ctx)
{
    r->x = 0;
    r->off = 0;
    r->n = n;
    r->wrap = wrap;
    r->fill = fill;
    r->ctx = ctx;
}

void cr_goto(colring *r, uint32_t x)
{
    uint16_t c, col;
    uint32_t wx;
    if (r->wrap) x %= r->wrap;
    r->x = x;
    r->off = (uint16_t)(x % r->n);
    for (c = 0, col = r->off, wx = x; c < r->n; c++) {
        r->fill(r->ctx, col, wx);
        if (++col == r->n) col = 0;
        if (++wx == r->wrap) wx = 0;    /* (never true without wrap: wrap is 0) */
    }
}

void cr_move(colring *r, uint32_t x)
{
    uint32_t right, left;               /* steps each way */
    if (r->wrap) {
        x %= r->wrap;
        right = x >= r->x ? x - r->x : x + r->wrap - r->x;
        left = r->wrap - right;
        if (right == 0) return;
        if (right <= left) left = 0;
        else right = 0;
    }
    else {
        if (x == r->x) return;
        right = x > r->x ? x - r->x : 0;
        left = x < r->x ? r->x - x : 0;
    }
    if (right >= r->n || left >= r->n) {
        cr_goto(r, x);
        return;
    }
    for (; right; right--) {            /* world x + n enters where x left */
        uint32_t wx = r->x + r->n;
        if (r->wrap && wx >= r->wrap) wx -= r->wrap;
        r->fill(r->ctx, r->off, wx);
        if (++r->x == r->wrap) r->x = 0;
        if (++r->off == r->n) r->off = 0;
    }
    for (; left; left--) {              /* world x - 1 enters at the left */
        r->x = r->x ? r->x - 1 : r->wrap - 1;   /* (no wrap: x > 0 here) */
        r->off = r->off ? r->off - 1 : r->n - 1;
        r->fill(r->ctx, r->off, r->x);
    }
}

uint16_t cr_col(const colring *r, uint32_t wx)
{
    uint32_t d;
    uint16_t col;
    if (r->wrap) {
        if (wx >= r->wrap) return CR_NONE;
        d = wx >= r->x ? wx - r->x : wx + r->wrap - r->x;
    }
    else {
        if (wx < r->x) return CR_NONE;
        d = wx - r->x;
    }
    if (d >= r->n) return CR_NONE;
    col = r->off + (uint16_t)d;
    if (col >= r->n) col -= r->n;
    return col;
}
