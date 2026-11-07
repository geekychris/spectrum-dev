/*
 * colring.h - a ring of columns over a wide world, for any layer that
 * scrolls in hardware (Layer 2, the tilemap): the layer holds n columns,
 * the world is as wide as you like (or wraps), and when the view moves only
 * the columns that come into view are drawn, by a callback.
 *
 * A column is whatever unit the layer has: a pixel column of Layer 2's
 * 320 x 256 mode (n = 320, see l2scroll.h), a tile column of the tilemap
 * (n = 40; beware: with the view as wide as the ring, the partly visible
 * tile columns at the left and right edges share one ring column at
 * sub-tile offsets: clip the layer one tile narrower, or make the world
 * repeat every n columns).
 *
 *   cr_init(r, n, wrap, fill, ctx)   n ring columns; wrap = the world's width
 *                                    in columns (0: no wrap; else >= n)
 *   cr_goto(r, x)                    world column x at the left edge: fills
 *                                    all n columns
 *   cr_move(r, x)                    scroll to x, either way (with wrap: the
 *                                    shorter way round), filling the columns
 *                                    that came into view; a move of n or more
 *                                    refills them all
 *   cr_col(r, wx)                    the ring column showing world column wx,
 *                                    or CR_NONE if it is not in view
 *   r->off                           the ring column at the left edge: the
 *                                    layer's scroll offset (in columns)
 *
 * fill(ctx, ring_col, wx) draws world column wx (0..wrap-1 with wrap) into
 * ring column ring_col.
 */
#ifndef COLRING_H
#define COLRING_H
#include <stdint.h>

#define CR_NONE 0xFFFF

typedef void (*cr_fill_fn)(void *ctx, uint16_t ring_col, uint32_t wx);

typedef struct {
    uint32_t x;          /* world column at the left edge */
    uint32_t wrap;       /* world width, 0 = no wrap */
    uint16_t off;        /* ring column at the left edge */
    uint16_t n;          /* ring width */
    cr_fill_fn fill;
    void *ctx;
} colring;

extern void cr_init(colring *r, uint16_t n, uint32_t wrap, cr_fill_fn fill, void *ctx);
extern void cr_goto(colring *r, uint32_t x);
extern void cr_move(colring *r, uint32_t x);
extern uint16_t cr_col(const colring *r, uint32_t wx);

#endif
