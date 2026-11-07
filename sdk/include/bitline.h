/*
 * bitline.h - lines and points on any 1-bit bitmap reached through a
 * row-address table: the 48K screen, or an off-screen buffer (a ship drawn
 * into 4 bytes x 24 lines, then shifted and blitted), clipped to a box,
 * no wrap-around. Bresenham in assembly (bitline.asm): ~45 T-states a
 * pixel inside the box, ~300 when each pixel is clipped. For the screen
 * alone, with wrap-around and compiled polygons, see line48.h.
 *
 *   bl_rows    the row-address table: zx_row (the screen, after gfx_init())
 *                by default; point it at your own table to draw into a buffer
 *                (e.g. a ship drawn into 4 bytes x 24 lines, then XOR-blitted)
 *   bl_cx0..1, bl_cy0..1   the clip box, inclusive (default 0..255, 0..191)
 *   bl_mode    BL_XOR (default), BL_OR or BL_CLEAR
 *
 * Coordinates are int16_t: lines may start or end off the box (each pixel is
 * tested then, so the cost is the whole length). bl_draw_open leaves out the
 * last pixel, and bl_poly draws polylines with it, so in XOR mode a shared
 * vertex is drawn once (not twice = erased).
 */
#ifndef ZX_BITLINE_H
#define ZX_BITLINE_H
#include <stdint.h>

#define BL_XOR   0
#define BL_OR    1
#define BL_CLEAR 2

extern uint8_t **bl_rows;
extern uint8_t bl_cx0, bl_cx1, bl_cy0, bl_cy1;
extern uint8_t bl_mode;

extern void bl_draw(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
extern void bl_draw_open(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
extern void bl_plot(int16_t x, int16_t y);
/* n points (x, y pairs of int8_t, relative to ox, oy) joined in order, and the
   last back to the first if closed; each vertex drawn once */
extern void bl_poly(const int8_t *xy, uint8_t n, int16_t ox, int16_t oy, uint8_t closed);
/* back to the screen and the full clip box (mode unchanged) */
extern void bl_screen(void);

#endif
