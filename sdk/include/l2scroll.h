/*
 * l2scroll.h - horizontal scrolling and parallax on Layer 2's 320 x 256
 * mode: the display is cut into bands of rows, each scrolling on its own
 * (Layer 2's X scroll, NextReg 0x16 / 0x71, rewritten by the copper at the
 * top of each band), over a world as wide as you like.
 *
 * Each band is a ring of 320 columns: when it moves, only the columns that
 * come into view are drawn, by a callback that fills one column of the band
 * for a world x. A column is 256 contiguous bytes in the 320 mode (byte y
 * of the column is row y), so a column fill is a few memset/loops; the rest
 * of the band costs nothing. Scrolling at 3 pixels a frame redraws 3 columns.
 *
 *   l2s_reset()                       no bands (all at offset 0)
 *   b = l2s_band(y0, y1, fill)        rows y0..y1 (inclusive), top to bottom,
 *                                     up to L2S_MAX bands; returns its number
 *   l2s_goto(b, x)                    world x at the band's left edge; draws
 *                                     all 320 columns (a new level, a jump)
 *   l2s_move(b, x)                    scroll to world x, either way, drawing
 *                                     the columns that came into view (a move
 *                                     of 320 or more redraws them all)
 *   l2s_redraw(b, wx)                 draw world column wx again if it is in
 *                                     view (the world changed there)
 *   l2s_offset(b)                     the band's scroll offset (x mod 320)
 *   l2s_copper()                      the scroll moves for every band, in row
 *                                     order, into the copper list being built
 *                                     (split_begin() ... split_end(), see
 *                                     nextsplit.h; upload it once a frame in
 *                                     the vertical blank)
 *
 * fill(b, wx, col): col points at row 0 of the Layer 2 column (mapped into
 * MMU slot l2w_slot), so the band's rows are col[y0] .. col[y1]; write those
 * only. Layer 2 must be set up with l2w_init() first; set l2w_slot and
 * l2w_restore if your program lives in slot 3 (0x6000-0x7FFF).
 *
 * The column that comes into view at the right edge is the one that just
 * left at the left edge: it is redrawn between frames, while the beam is
 * elsewhere, but a column drawn while the beam crosses the band shows one
 * frame torn at the edge pixel (hidden by a clip window if it matters).
 */
#ifndef L2SCROLL_H
#define L2SCROLL_H
#include <stdint.h>

#define L2S_MAX 4
#define L2S_W 320

typedef void (*l2s_fill_fn)(uint8_t band, uint32_t wx, uint8_t *col);

extern void l2s_reset(void);
extern uint8_t l2s_band(uint8_t y0, uint8_t y1, l2s_fill_fn fill);
extern void l2s_goto(uint8_t b, uint32_t x);
extern void l2s_move(uint8_t b, uint32_t x);
extern void l2s_redraw(uint8_t b, uint32_t wx);
extern uint16_t l2s_offset(uint8_t b);
extern void l2s_copper(void);

#endif
