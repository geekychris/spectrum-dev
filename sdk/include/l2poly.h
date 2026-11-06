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

#endif
