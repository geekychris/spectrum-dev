/*
 * l2line.h - lines on the 256 x 192 Layer 2 back buffer (nextgfx.h's double
 * buffer), Bresenham in Z80N assembly (sdk/src/next/l2line.asm): about 30
 * T-states a pixel. Endpoints are int16 and clipped to the screen; both
 * ends are drawn.
 *
 *   l2l_line(x0, y0, x1, y1, colour)
 *
 * It maps the back buffer's 8K pages into MMU slot 3 (0x6000-0x7FFF) as it
 * goes and puts the slot back after, so it can be used between l2poly's
 * l2p_begin() and l2p_end() or on its own. Test: projects/nextlibtest.
 */
#ifndef L2LINE_H
#define L2LINE_H
#include <stdint.h>

extern void l2l_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t colour);

#endif
