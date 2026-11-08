/*
 * texview.h - a picture drawn through a table made on the host: for every
 * pixel of a shape, the texel of a texture it shows and its shade. The
 * shape can be anything a host can work out pixel by pixel (Boing Twist's
 * ball: a sphere in perspective at a tilt, lit by a fixed light); turning
 * it about one axis is an add to u (tv_off), and the texture can be changed
 * at will (a picture cut into pieces and put together again).
 *
 * The table, the "view", is made by sdk/tools/texview.py (Stream; model()
 * is a reference renderer for tests) and put in 8K pages of the .nex: rows
 * of (dy, dx0, n) relative to a centre, then n records of (u, v, shade).
 *
 *   texture   up to 256 x 96 texels, rows of 256, in 3 consecutive 8K pages
 *             (tv_tex): pixel = texture[v][(u + tv_off) & 255] | shade
 *   output    a 16K bank (tv_bank: Layer 2's back buffer, or any bank: a
 *             second texture made through another view) written through
 *             Layer 2's write paging, rows of 256 bytes, the bank's three
 *             thirds = rows 0-191; rows from tv_rows (default 192) and
 *             columns off either side are left out, so a view can be drawn
 *             anywhere (tv_cx, tv_cy: where its centre goes)
 *   memory    tv_draw maps the texture at 0x0000-0x5FFF and the view at
 *             0x6000-0x7FFF and puts MMU slots 0-3 back as they were; it
 *             must be called from code at 0x8000 up. While it runs (and
 *             while tv_fill / tv_copy write the window) writes to
 *             0x0000-0x3FFF go to the bank: keep variables out of there.
 *   speed     68 T-states a pixel (an unrolled loop entered per row; about
 *             80 measured in ZEsarUX at 28 MHz), ~300 a row
 *
 *   tv_or_span(y, x, n, bits)   OR bits into n pixels of row y of tv_bank
 *                               (reads and writes through the window:
 *                               shadows, with a darker palette copy at
 *                               index | bits)
 *   tv_fill(dst, v, n)          fill n bytes, never reading them: memset's
 *                               LDIR reads what it wrote, and through the
 *                               window that is whatever is mapped for reading
 *   tv_copy(src, dst, n, turned) n bytes from src (mapped for reading) to dst
 *                               (the window, say), backwards from src when
 *                               turned: a texture made of pieces turned round
 *
 * Opening the window yourself: NextReg 0x13 = the bank, port 0x123B =
 * 0x0B | third << 6 (write, Layer 2 visible, NextReg 0x13's bank); 0x02 to
 * close it. Test: projects/texviewtest.
 */
#ifndef TEXVIEW_H
#define TEXVIEW_H
#include <stdint.h>

extern uint8_t tv_page;                  /* the view's first 8K page */
extern int16_t tv_cx, tv_cy;             /* where its centre goes */
extern uint8_t tv_off;                   /* added to every u */
extern uint8_t tv_bank;                  /* the 16K bank written */
extern uint8_t tv_tex;                   /* the texture's first 8K page */
extern uint8_t tv_rows;                  /* rows drawn: 0 .. tv_rows - 1 (192) */
extern uint16_t tv_pixels;               /* pixels the last tv_draw wrote */

extern void tv_draw(void);
extern void tv_or_span(uint8_t y, uint8_t x, uint8_t n, uint8_t bits);
extern void tv_fill(uint8_t *dst, uint8_t v, uint16_t n);
extern void tv_copy(const uint8_t *src, uint8_t *dst, uint8_t n, uint8_t turned);
#endif
