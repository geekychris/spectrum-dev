/* Grid pathfinding through a distance field (see gridbfs.h). */
#include <stdint.h>
#include <string.h>
#include "gridbfs.h"

/* shared with gridbfs_run.asm */
uint8_t *gb_cells;              /* (flags, dist) pairs, row stride gb_stride2 bytes */
uint16_t gb_stride2;            /* 2 * (w + 1) */
uint16_t *gb_qhead, *gb_qtail;  /* the search queue: a ring of cell offsets in one 256-byte page */
uint8_t *gb_want;               /* gb_run stops once this cell's distance is known */
uint16_t gb_searched;
uint8_t gb_overflow;

uint16_t *gb_queue;
static uint8_t gb_w, gb_h, gb_valid;
static uint8_t gb_tx = 255, gb_ty = 255;

#define CELL(x, y) (gb_cells + (uint16_t)((y) + 1) * gb_stride2 + 2 * (uint16_t)(x))

#ifdef __SDCC
extern void gb_run(void);
#else
/* the next slot of the ring (the asm only increments the address's low byte) */
static uint16_t *next(uint16_t *q)
{
    return q + 1 == gb_queue + GB_QUEUE / 2 ? gb_queue : q + 1;
}

static void visit(uint8_t *p, uint8_t bit, uint8_t d)
{
    if ((p[0] & bit) && p[1] == 255) {
        p[1] = d;
        if (p[0] & GB_IN) {
            *gb_qtail = (uint16_t)(p - gb_cells);
            gb_qtail = next(gb_qtail);
            if (gb_qtail == gb_qhead) gb_overflow = 1;
        }
    }
}

static void gb_run(void)
{
    while (gb_qhead != gb_qtail && gb_want[1] == 255) {
        uint8_t *m = gb_cells + *gb_qhead;
        gb_qhead = next(gb_qhead);
        uint8_t d = m[1] + 1;
        gb_searched++;
        if (d == 255) { gb_qhead = gb_qtail; return; }
        visit(m + 2, GB_L, d);
        visit(m - 2, GB_R, d);
        visit(m + gb_stride2, GB_U, d);
        visit(m - gb_stride2, GB_D, d);
    }
}
#endif

void gb_init(uint8_t *cells, uint16_t *queue, uint8_t w, uint8_t h)
{
    gb_cells = cells;
    gb_queue = queue;
    gb_w = w;
    gb_h = h;
    gb_stride2 = 2 * ((uint16_t)w + 1);
    memset(cells, 0, GB_CELLS(w, h));
    gb_valid = 0;
    gb_tx = gb_ty = 255;
    gb_searched = 0;
    gb_overflow = 0;
}

void gb_set(uint8_t x, uint8_t y, uint8_t flags)
{
    uint8_t *c = CELL(x, y);
    if (*c != flags) {
        *c = flags;
        gb_valid = 0;
    }
}

uint8_t gb_get(uint8_t x, uint8_t y)
{
    return *CELL(x, y);
}

void gb_target(uint8_t x, uint8_t y)
{
    if (x != gb_tx || y != gb_ty) {
        gb_tx = x;
        gb_ty = y;
        gb_valid = 0;
    }
}

static void field_start(void)
{
    uint8_t *t, *c = gb_cells + 1, *end = gb_cells + GB_CELLS(gb_w, gb_h);
    for (; c < end; c += 2) *c = 255;
    gb_qhead = gb_qtail = gb_queue;
    gb_overflow = 0;
    t = CELL(gb_tx, gb_ty);
    t[1] = 0;
    if (t[0] & GB_IN) *gb_qtail++ = (uint16_t)(t - gb_cells);
    gb_valid = 1;
}

static uint8_t *known(uint8_t x, uint8_t y)
{
    uint8_t *s = CELL(x, y);
    if (!gb_valid) field_start();
    if (s[1] == 255 && gb_qhead != gb_qtail) {
        gb_want = s;
        gb_run();
    }
    return s;
}

uint8_t gb_dist(uint8_t x, uint8_t y)
{
    uint8_t d = known(x, y)[1];
    return gb_overflow ? 255 : d;
}

uint8_t gb_step(uint8_t x, uint8_t y)
{
    uint8_t *s = known(x, y), d = s[1] - 1, f = s[0];
    if (s[1] == 0) return 0;
    if (s[1] == 255 || gb_overflow) return GB_NONE;
    /* the neighbours one step closer are already labelled (BFS order) */
    if ((f & GB_L) && (s[-2] & GB_IN) && s[-1] == d) return GB_L;
    if ((f & GB_R) && (s[2] & GB_IN) && s[3] == d) return GB_R;
    if ((f & GB_U) && (*(s - gb_stride2) & GB_IN) && *(s - gb_stride2 + 1) == d) return GB_U;
    if ((f & GB_D) && (*(s + gb_stride2) & GB_IN) && *(s + gb_stride2 + 1) == d) return GB_D;
    return GB_NONE;
}
