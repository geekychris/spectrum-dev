/*
 * l2fill.h - filled convex polygons on the 256 x 192 Layer 2 back buffer
 * (nextgfx.h's double buffer, or any bank set in l2_back), for scenes of
 * many small polygons: Marble Tilt draws its 3D board (about 150 quads)
 * with it. Z80N assembly.
 *
 *   l2f_begin()                 before drawing: maps Layer 2 pages into MMU
 *                               slot 3 (0x6000-0x7FFF) as it goes
 *   l2f_poly(xy, n, colour)     xy = x0, y0, x1, y1, ... (int16_t pairs, n
 *                               3..255), either winding, anything off the
 *                               screen clipped; rows ytop <= y < ybot, each
 *                               from floor(x left) to floor(x right) with
 *                               edges stepped from their top vertex, so two
 *                               polygons sharing an edge leave no gap
 *   l2f_end()                   puts slot 3 back
 *   l2f_top, l2f_bot            rows drawn: l2f_top <= y < l2f_bot (0..192)
 *
 * Each edge is stepped once (16.8 fixed point; the slope from a reciprocal
 * table and the Z80N's MUL) into a table of rows, then each row is filled
 * between its two edges: spans up to 32 pixels by an unrolled loop (11
 * T-states a pixel), longer ones by the zxnDMA (don't use the DMA between
 * l2f_begin and l2f_end). About 1,500 T-states a polygon and 300 a row,
 * against l2poly's 8,200 and 760 (l2poly has bands and rectangles too).
 * Concave polygons are filled wrongly. Test: projects/nextlibtest.
 */
#ifndef L2FILL_H
#define L2FILL_H

#include <stdint.h>

extern uint8_t l2f_top, l2f_bot;
extern void l2f_begin(void);
extern void l2f_end(void);
extern void l2f_poly(const int16_t *xy, uint8_t n, uint8_t colour);

#endif
