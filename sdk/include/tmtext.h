/*
 * tmtext.h - text on the tilemap (nexttile.h) that is only written when it
 * changes: a game says what each "slot" (the score, a message, a menu
 * line...) shows every frame, and tmtext rewrites a slot's cells only when
 * its text, place, colour or size differ from last time. Big letters (2x to
 * 4x, tm_glyphnx) are made from the same font on first use and kept in a
 * pool of tiles until tmt_clear().
 *
 *   tmt_init(slots, n, font, font_tile, pool_tile)
 *                               slots: the game's array of n tmt_slot;
 *                               font: 96 glyphs of 8 bytes (characters
 *                               32..127, top row first, bit 7 left), already
 *                               made into tiles font_tile.. at 1x (e.g. with
 *                               tm_glyphnx); big letters go in tiles
 *                               pool_tile..255
 *   tmt_say(id, col, row, s, pal, scale, shadow)
 *                               slot id shows s at (col, row) (col
 *                               TMT_CENTRE: centred) in palette pal (tile
 *                               attribute offset), scale 1 = the font's
 *                               tiles, 2-4 big letters (shadow: pen 2, 2 px
 *                               down-right); spaces stay transparent
 *   tmt_quiet(id)               slot id shows nothing
 *   tmt_clear()                 every cell blank, every slot empty, the
 *                               big-letter pool emptied (a new screen)
 *   tmt_num(dst, v)             v in decimal at dst, returns the end (the 0)
 *
 * Writes go through nexttile (MMU slot 2), so strings can be anywhere but
 * 0x4000-0x5FFF. Overlapping slots erase each other's cells: keep them apart.
 * Test: projects/texttest.
 */
#ifndef TMTEXT_H
#define TMTEXT_H

#include <stdint.h>

#define TMT_LEN 40
#define TMT_CENTRE 0xFF

typedef struct { char s[TMT_LEN + 1]; uint8_t col, row, len, sc, pal, sh; } tmt_slot;

extern void tmt_init(tmt_slot *slots, uint8_t n, const uint8_t *font, uint8_t font_tile, uint8_t pool_tile);
extern void tmt_say(uint8_t id, uint8_t col, uint8_t row, const char *s, uint8_t pal, uint8_t scale, uint8_t shadow);
extern void tmt_quiet(uint8_t id);
extern void tmt_clear(void);
extern char *tmt_num(char *dst, int32_t v);

#endif
