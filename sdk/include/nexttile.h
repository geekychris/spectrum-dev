/*
 * nexttile.h - the Next's tilemap as a text / HUD layer: 40 x 32 cells of
 * 8 x 8 pixels covering the whole 320 x 256 display, 4 bpp tiles, each cell
 * with its own palette offset (16 colours), drawn over the other layers when
 * the layer order puts the ULA/tilemap on top (NextReg 0x15, e.g. U S L).
 * Colour 0 of a tile is transparent, so text floats over the game.
 *
 * Memory: in 16K bank 5 (the ULA screen's bank; the ULA is switched off):
 * the map (40 x 32 x 2 bytes: tile, attribute) at offset 0, the tile
 * definitions (32 bytes each, 256 tiles) at offset 0x0A00. Writes map bank
 * 5's pages into MMU slot 2 only and put it back, so the strings and data
 * you pass can be anywhere but 0x4000-0x5FFF.
 *
 *   tm_init()                    ULA off, tilemap on (40 x 32, attributes),
 *                                every cell tile 0 (define it blank)
 *   tm_def(tile, defs, n)        n tile definitions (32 bytes each)
 *   tm_font(tile, font, n, fg, bg)
 *                                n 8 x 8 one-bit glyphs (8 bytes each, top
 *                                bit left) as tiles: set pixels pen fg, the
 *                                rest pen bg (0 = transparent)
 *   tm_glyph2x(tile, glyph, fg, shadow)
 *                                a glyph at double size as 4 tiles (top-left,
 *                                top-right, bottom-left, bottom-right), with a
 *                                drop shadow 2 pixels down-right in pen
 *                                shadow (0: none)
 *   tm_put(col, row, tile, pal)  one cell; pal = palette offset 0..15
 *   tm_print(col, row, s, first, pal)
 *                                a string, character c -> tile first + c - 32
 *   tm_fill(col, row, w, h, tile, pal)
 *   tm_clear()                   every cell tile 0
 *   tm_scroll(x, y)              pixel offset of the whole layer
 *   tm_pal(pal, pen, rgb9)       tilemap palette: pen 0..15 of offset pal
 */
#ifndef NEXTTILE_H
#define NEXTTILE_H

#include <stdint.h>

#define TM_COLS 40
#define TM_ROWS 32

extern void tm_init(void);
extern void tm_def(uint8_t tile, const uint8_t *defs, uint8_t n);
extern void tm_font(uint8_t tile, const uint8_t *font, uint8_t n, uint8_t fg, uint8_t bg);
extern void tm_glyph2x(uint8_t tile, const uint8_t *glyph, uint8_t fg, uint8_t shadow);
extern void tm_put(uint8_t col, uint8_t row, uint8_t tile, uint8_t pal);
extern void tm_print(uint8_t col, uint8_t row, const char *s, uint8_t first, uint8_t pal);
extern void tm_fill(uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t tile, uint8_t pal);
extern void tm_clear(void);
extern void tm_scroll(uint16_t x, uint8_t y);
extern void tm_pal(uint8_t pal, uint8_t pen, uint16_t rgb9);

#endif
