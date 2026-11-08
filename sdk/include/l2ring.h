/*
 * l2ring.h - a picture much bigger than the screen on Layer 2 (320 x 256 x
 * 8 bpp), scrolled any way at once: Layer 2 holds the part on screen as a
 * ring (picture column x is Layer 2 column x mod 320, row y is row y mod
 * 256), the hardware scroll (NextReg 0x16, 0x71, 0x17) shows it from the
 * right place, and a move draws only the columns and rows that came into
 * view. Outside the picture is its void colour.
 *
 * The picture is made on the host by sdk/tools/l2ring.py (encode): palette
 * indices run-length coded down every column and along every row, with a
 * directory of both, in 8K pages from a first page (the .nex loads them;
 * ~1 byte in 10 for flat-shaded 3D). Rolling Steel's courses are 1450-2160
 * pixels by 470-1020, 7-8 pages each with their palettes.
 *
 *   lr_open(page)        the picture whose header is at that 8K page:
 *                        lr_w, lr_h, its palette (lr_palette), and
 *                        lr_ox, lr_oy (two numbers of the maker's, e.g.
 *                        where the picture's 0, 0 is in some other space)
 *   lr_goto(x, y)        the screen's top left at picture pixel (x, y),
 *                        any coordinates (off the picture is the void): a
 *                        move of up to 64 pixels draws the new columns
 *                        (~4,000 T-states each) and rows (~9,000 each), a
 *                        jump redraws the screen; then the scroll registers
 *   lr_redraw()          the next lr_goto redraws everything (a new picture)
 *   lr_palette(dst, max) the picture's palette (9-bit RGB333) into dst;
 *                        returns how many
 *   lr_x, lr_y           where the screen is; lr_cols, lr_rows count what
 *                        was drawn (for your perf figures; reset them)
 *   lr_l2page            Layer 2's first 8K page (18 = bank 9, the default)
 *   lr_col(c, x, y0), lr_row(c0, x0, y)
 *                        the drawing: Layer 2 column c = picture column x,
 *                        rows y0..y0 + 255; Layer 2 row y & 255 = picture
 *                        row y, columns x0.. (c0 = x0 mod 320)
 *
 * Layer 2 must be in 320 x 256 mode, from lr_l2page. It reads the picture
 * through MMU slot 2 and writes Layer 2 through slot 1, giving both back.
 * Test: projects/ringtest (Layer 2 against l2ring.py's model after moves in
 * every direction and jumps).
 */
#ifndef L2RING_H
#define L2RING_H
#include <stdint.h>

extern int16_t lr_w, lr_h, lr_ox, lr_oy, lr_x, lr_y;
extern uint8_t lr_void, lr_page, lr_l2page;
extern uint16_t lr_cols, lr_rows;

extern void lr_open(uint8_t page);
extern void lr_goto(int16_t x, int16_t y);
extern void lr_redraw(void);
extern uint8_t lr_palette(uint16_t *dst, uint8_t max);
extern void lr_col(uint16_t c, int16_t x, int16_t y0);
extern void lr_row(uint16_t c0, int16_t x0, int16_t y);

#endif
