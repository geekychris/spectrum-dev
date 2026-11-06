/*
 * font5.h - a 5x7 bitmap font for drawing text yourself (Layer 2, sprites,
 * a dot-matrix display): characters '!'..'Z' (no lower case; space is
 * blank), 7 rows each, pixels in bits 7..3 (bit 7 = leftmost). A character
 * cell is 6 x 8 at scale 1. Drawing: l2_text5() (nextgfx.h) on the Next's
 * 256 x 192 Layer 2, l2w_text5() in its 320 x 256 mode.
 */
#ifndef ZX_FONT5_H
#define ZX_FONT5_H
#include <stdint.h>
#define FONT5_W 5
#define FONT5_H 7
#define FONT5_FIRST '!'
#define FONT5_LAST  'Z'
#define FONT5_CHARS (FONT5_LAST - FONT5_FIRST + 1)
extern const uint8_t font5x7[FONT5_CHARS][FONT5_H];
/* the glyph of c, or 0 (space and anything not in the font) */
#define font5_glyph(c) ((c) >= FONT5_FIRST && (c) <= FONT5_LAST ? font5x7[(c) - FONT5_FIRST] : (const uint8_t *)0)
/* pixel width of s at `scale` (no trailing gap) */
extern uint16_t font5_width(const char *s, uint8_t scale);
#endif
