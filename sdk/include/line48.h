/*
 * line48.h - fast lines, wireframe polygons and point lists on the 48K
 * screen (Bresenham in assembly, sdk/src/line48.asm).
 *
 *   line48_init();                 once, after gfx_init(): row tables at 0xF200-0xF3FF
 *   line_mode = LINE_XOR;          XOR (draw = erase), OR (set) or CLEAR (reset)
 *   line_draw(10, 20, 200, 150);   screen coordinates, both ends drawn
 *   line_clip(-40, 10, 300, 90);   int16 coordinates, clipped to the screen
 *   line_poly(cx, cy, pts, n, 1);  closed polygon of int8 offsets around (cx, cy)
 *   plot_list(xy, n);              n points from x,y byte pairs (y >= 192 skipped)
 *
 * line_poly wraps around the screen edges (x mod 256, y mod 192): a shape
 * half off the right edge shows its other half on the left, which is what a
 * wrap-around playfield wants. Offsets must stay within -32..+32 of the
 * centre (the row table covers y -32..223 once). Each edge leaves out its
 * last pixel, so in XOR mode the vertices don't cancel, and drawing the same
 * polygon twice erases it exactly, whatever was drawn in between.
 *
 * Cost (3.5 MHz): about 80 T-states a pixel along x, 85-110 along y, plus
 * ~450 T-states of setup a line: a 16-pixel radius octagon is ~10K T-states.
 *
 * Memory: 0xF200-0xF3FF (between frame_init's IM 2 table at 0xF000-0xF1F3 and
 * the stack, which z88dk puts at 0xFF58). The code modifies itself (patched
 * octant and mode opcodes), so it must run from RAM, as all z88dk programs do.
 */
#ifndef LINE48_H
#define LINE48_H
#include <stdint.h>

#define LINE_XOR   0
#define LINE_OR    1
#define LINE_CLEAR 2

typedef struct { int8_t x, y; } vec_pt;     /* a vertex offset (also vec48.h) */

extern uint8_t line_mode;                    /* LINE_XOR / LINE_OR / LINE_CLEAR */

extern void line48_init(void);
/* both ends drawn; x 0..255, y 0..191 */
extern void line_draw(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
/* clipped to the screen (0..255 x 0..191); returns 0 if nothing was visible */
extern uint8_t line_clip(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
/* n vertices at (cx + pts[i].x, cy + pts[i].y), wrapping around the screen;
   closed: n edges back to the first vertex, else n-1 edges and the last point */
extern void line_poly(uint8_t cx, uint8_t cy, const vec_pt *pts, uint8_t n, uint8_t closed);
/* n pixels from x,y pairs; pairs with y >= 192 are skipped */
extern void plot_list(const uint8_t *xy, uint8_t n);

#endif
