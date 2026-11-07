/*
 * shadow48.h - a shadow screen for flicker-free 48K sprites (sdk/src/shadow48.asm).
 *
 * The shadow is a second copy of the screen, bitmap and attributes, at
 * 0x6000-0x7AFF, laid out exactly like the screen (shadow address = screen
 * address | 0x2000), that holds the background without sprites. A frame then:
 *
 *   1. restores the sprites' old rectangles from the shadow (spr8_restore,
 *      and spr8_attr_restore for their colours),
 *   2. draws the sprites at their new places (spr8_draw, masked) and colours
 *      their cells (spr8_attr_fill) - spr8_frame does both for a list,
 *
 * all right after the frame interrupt, while the beam is in the top border
 * (~14,000 T-states), so nothing is ever seen half drawn. Background changes
 * (an eaten dot, a blinking item) are made in the shadow (spr8_plane =
 * SHADOW_PLANE) and shown with spr8_restore / shadow_show_rect.
 *
 * Sprites are 8 pixels wide, any height, shifted at draw time (no
 * pre-shifted copies, ~230 T-states a line): an image is { h, then h x
 * (mask, data) }, mask bits = pixels the sprite covers (cleared before the
 * data is ORed in).
 * x must be <= 247 (a sprite always writes two bytes), y + h <= 192.
 *
 * Uses gfx_init()'s zx_row table. Keep the program out of 0x6000-0x7AFF
 * (the default ORG is 32768; the BASIC loader ends near 0x5D00).
 */
#ifndef SHADOW48_H
#define SHADOW48_H

#include <stdint.h>

#define SHADOW_SCREEN 0x00               /* spr8_plane values */
#define SHADOW_PLANE  0x20
#define SHADOW_ADDR   ((uint8_t *)0x6000)
#define SHADOW_ATTR   ((uint8_t *)0x7800)

extern uint8_t spr8_plane;               /* where spr8_draw writes: screen (0) or shadow */

/* masked 8-px sprite: screen = (screen & ~(mask >> s)) | (data >> s), s = x & 7 */
extern void spr8_draw(uint16_t xy, const uint8_t *img);   /* xy = x | y << 8 */
/* copy the two bytes under an 8-px sprite at (x, y), h lines, shadow -> screen */
extern void spr8_restore(uint16_t xy, uint16_t h);
#define SPR8_XY(x, y) ((uint16_t)(uint8_t)(x) | ((uint16_t)(y) << 8))

/* the attribute cells under a w x h pixel box at (x, y), wh = w | h << 8:
   set to attr / copied from the shadow's attributes */
extern void spr8_attr_fill(uint16_t xy, uint16_t wh, uint16_t attr);
extern void spr8_attr_restore(uint16_t xy, uint16_t wh);

/* a sprite for the batch calls: on, position, width in pixels (for the
   colour cells), image, colour */
typedef struct {
    uint8_t on, x, y, w;
    const uint8_t *img;
    uint8_t attr;
} spr8_obj;
/* a frame of sprites: erase where each object of drawn[] (on) was drawn,
   pixels and, if its cells or colour change, colours, from the shadow, then
   spr8_draw_all(now). n <= 16. Keep two lists and swap them each frame
   (now becomes the next frame's drawn). */
extern void spr8_frame(const spr8_obj *drawn, const spr8_obj *now, uint16_t n);
/* draw each object (on), top first (so, run at the start of a frame, each is
   finished before the beam gets there), and colour its cells. n <= 16 */
extern void spr8_draw_all(const spr8_obj *o, uint16_t n);

extern void shadow_save(void);           /* screen -> shadow (bitmap + attributes) */
extern void shadow_show(void);           /* shadow -> screen */
/* shadow -> screen for byte columns c0..c0+w-1 of lines y..y+h-1 */
extern void shadow_show_rect(uint8_t c0, uint8_t y, uint8_t w, uint8_t h);

#endif
