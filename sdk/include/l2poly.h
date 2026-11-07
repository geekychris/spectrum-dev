/*
 * l2poly.h - filled polygons on the 256 x 192 Layer 2 back buffer (nextgfx.h's
 * double buffer), in Z80N assembly: convex polygons of up to 16 vertices with
 * 16-bit screen coordinates (anything off screen is clipped), and spans.
 *
 *   l2p_begin()                    before drawing: maps Layer 2 pages into MMU
 *                                  slot 3 (0x6000-0x7FFF) as it goes
 *   l2p_poly(xy, n, colour)        xy = x0, y0, x1, y1, ... (int16_t pairs),
 *                                  either winding; rows y with ytop <= y < ybot,
 *                                  each from floor(x left) to floor(x right)
 *   l2p_span(y, x0, x1, colour)    one row, x0..x1 inclusive, clipped
 *   l2p_band(ytop, ybot, xtop, xbot, nr, spans, ns)
 *                                  strips that share their top and bottom rows
 *                                  (a road's cross-section between two depths):
 *                                  nr rails, rail k a line from (xtop[k], ytop) to
 *                                  (xbot[k], ybot), and ns spans
 *                                  (left rail, right rail, colour), filled in
 *                                  order on each row ytop <= y < ybot (either
 *                                  edge may be the lower: they swap). One row
 *                                  loop for all of them: much cheaper than ns
 *                                  quads. At most L2P_RAILS rails, L2P_SPANS spans.
 *                                  Bands clip to 0..255 (not l2p_xmin / xmax).
 *   l2p_end()                      puts slot 3 back
 *
 *   l2p_top, l2p_bot               rows drawn: l2p_top <= y < l2p_bot (0..192)
 *   l2p_xmin, l2p_xmax             columns drawn (0..255)
 *
 * Edges step in 16.16 fixed point; the slope comes from a reciprocal table
 * and the Z80N's MUL for edges up to 255 rows high (a division otherwise).
 * Spans shorter than 40 pixels are stored by an unrolled loop (11 T-states a
 * pixel), longer ones by the zxnDMA (~2 T-states a pixel + ~450 to set up).
 * Concave or self-crossing polygons are filled between their outermost
 * edges' chains, not exactly. Test: projects/nextlibtest (l2poly checks).
 */
#ifndef L2POLY_H
#define L2POLY_H

#include <stdint.h>

extern uint8_t l2p_top, l2p_bot, l2p_xmin, l2p_xmax;

extern void l2p_begin(void);
extern void l2p_end(void);
extern void l2p_poly(const int16_t *xy, uint8_t n, uint8_t colour);
extern void l2p_span(uint8_t y, int16_t x0, int16_t x1, uint8_t colour);
#define L2P_RAILS 32
#define L2P_SPANS 32
extern void l2p_band(int16_t ytop, int16_t ybot, const int16_t *xtop, const int16_t *xbot,
                     uint8_t nr, const uint8_t *spans, uint8_t ns);
/* fill rows y0 <= y < y1 between x0 and x1 (a rectangle, clipped) */
extern void l2p_rect(int16_t y0, int16_t y1, int16_t x0, int16_t x1, uint8_t colour);

#endif
