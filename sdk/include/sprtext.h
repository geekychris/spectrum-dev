/*
 * sprtext.h - big text from the Next's hardware sprites: the 5x7 font
 * (font5.h) made into 16 x 16 patterns at start-up, one sprite a
 * character, enlarged by the sprites' zoom (x2, x4, x8). For titles and
 * messages over a Layer 2 picture: nothing is drawn into Layer 2, so
 * nothing has to be put back, and a double-buffered screen needs no
 * redraw for them.
 *
 *   sprtext_init(first, chars)    patterns first.. for each character of
 *                                 chars (e.g. "ABC!"): pixel 1 the glyph,
 *                                 2 a shadow one pixel down and right,
 *                                 the rest transparent (0xE3)
 *   sprtext_colour(pal, rgb9)     the sprite palette for palette offset
 *                                 pal (0..15): the glyph rgb9, the shadow
 *                                 black
 *   sprtext_show(first, x, y, s, pal, zoom)
 *                                 s from sprite first on, its top-left at
 *                                 (x, y) on the 320 x 256 sprite plane,
 *                                 zoom 0..3 (x1..x8); a character advances
 *                                 6 << zoom pixels; spaces and characters
 *                                 not given to sprtext_init take no
 *                                 sprite. Returns the next free sprite.
 *   sprtext_width(s, zoom)        its width in pixels (font5_width)
 *
 * Hide them with sprg_hide (nextspr.h).
 */
#ifndef SPRTEXT_H
#define SPRTEXT_H
#include <stdint.h>

extern void sprtext_init(uint8_t first, const char *chars);
extern void sprtext_colour(uint8_t pal, uint16_t rgb9);
extern uint8_t sprtext_show(uint8_t first, int16_t x, int16_t y, const char *s, uint8_t pal, uint8_t zoom);
extern uint16_t sprtext_width(const char *s, uint8_t zoom);

#endif
