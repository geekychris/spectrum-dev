/*
 * hud.c - Lander 3D's text and big letters (the original's render.c hud(),
 * titles, result boxes, help pages) on the Next's other layers, over the
 * view:
 *
 *   the tilemap: the original's 8 x 8 text (the Spectrum's ROM font with
 *     the original's drop shadow, in its pens), panels, the fuel gauge
 *   sprites: the original's 16 x 16 font for the big text, each letter
 *     over its shadow
 *
 * Coordinates are the original's 320 x 256 screen: the display with its
 * border, Layer 2's view in the middle 256 x 192.
 * (Orbit Docking's hud.c, with this game's pens.) In bank 24 with screens.c.
 */
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "hud.h"
#include "pens.h"
#include "gfx_data.h"

#define T_FONT   1                       /* tiles 1..96: characters 32..127 */
#define T_PANEL  97                      /* pen 3 */
#define T_EDGE   98                      /* 98..105: a panel's border */
#define T_PFONT  112                     /* 112..207: the font on a panel */
#define T_FUEL_M 216                     /* a gauge tile, 0..8 pixels filled */
#define S_BIG    0                       /* group g: shadows S_BIG + g * 24.., ink + 12 */
#define BIG_MAX  12

static uint16_t rgb9(uint16_t c)        /* RGB444 -> RGB333 */
{
    return (uint16_t)(((c >> 9) & 7) << 6 | ((c >> 5) & 7) << 3 | ((c >> 1) & 7));
}

/* the inks: the original's pens 1 white, 24 amber, 23 red, 25 green, 26
   cyan, 28 HUD grey, 0 black, 11-13 the pad rings (render.c) */
static const uint16_t ink_rgb[TX_INKS] = { 0xFFF, 0xFB2, 0xF33, 0x4E4, 0x3DF, 0x99A, 0x000, 0x3E5, 0xFE3, 0xE3E };

/* a 4-bit tile from an 8 x 8 glyph: pen 1 the glyph, pen 2 its shadow a
   pixel down and right (g_text_shadow) */
static void shadow_tile(uint8_t tile, const uint8_t *g, uint8_t bg)
{
    uint8_t def[32], y, x, *d = def;
    for (y = 0; y < 8; y++) {
        uint8_t row = g[y], sh = y ? g[y - 1] >> 1 : 0;
        for (x = 0; x < 8; x += 2, d++) {
            uint8_t m0 = 0x80 >> x, m1 = 0x40 >> x;
            uint8_t a = row & m0 ? 1 : sh & m0 ? 2 : bg, b = row & m1 ? 1 : sh & m1 ? 2 : bg;
            *d = a << 4 | b;
        }
    }
    tm_def(tile, def, 1);
}

static void fill_tile(uint8_t tile, uint8_t pen)
{
    uint8_t def[32];
    memset(def, pen * 17, 32);
    tm_def(tile, def, 1);
}

/* a fuel gauge tile: rows 0 and 7 the box's edge (pen 5), between them n
   pixels from x0 filled (pen 1, the ink), the rest black (pen 6); edge_x
   a column of edge (or 8: none) */
static void fuel_tile(uint8_t tile, uint8_t x0, uint8_t n, uint8_t edge_x, uint8_t box)
{
    uint8_t def[32], x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            uint8_t pen = x == edge_x ? 5 : !box ? 0 : (y == 0 || y == 7) ? 5 : (x >= x0 && x < x0 + n) ? 1 : 6;
            if (x & 1) def[y * 4 + x / 2] |= pen; else def[y * 4 + x / 2] = pen << 4;
        }
    tm_def(tile, def, 1);
}

void hud_init(void)
{
    uint8_t k;
    static const uint8_t edges[7][8] = {
        { 0xFF, 0xFF, 0, 0, 0, 0, 0, 0 },                    /* top */
        { 0, 0, 0, 0, 0, 0, 0xFF, 0xFF },                    /* bottom */
        { 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0 },  /* left */
        { 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 },  /* right */
        { 0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0 },  /* top left */
        { 0xFF, 0xFF, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 },  /* top right */
        { 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF },  /* bottom left */
    };
    static const uint8_t br[8] = { 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0xFF, 0xFF };
    tm_init();
    for (k = 0; k < 96; k++) {
        shadow_tile(T_FONT + k, rom_font + k * 8, 0);
        shadow_tile(T_PFONT + k, rom_font + k * 8, 3);
    }
    fill_tile(T_PANEL, 3);
    for (k = 0; k < 7; k++) tm_font(T_EDGE + k, edges[k], 1, 4, 3);
    tm_font(T_EDGE + 7, br, 1, 4, 3);
    for (k = 0; k < 9; k++) fuel_tile(T_FUEL_M + k, 0, k, 8, 1);
    for (k = 0; k < TX_INKS; k++) {
        tm_pal(k, 1, rgb9(ink_rgb[k]));
        tm_pal(k, 2, 0);
        tm_pal(k, 3, rgb9(0x223));
        tm_pal(k, 4, rgb9(0x99A));           /* a panel's border */
        tm_pal(k, 5, rgb9(0x99A));           /* the fuel gauge's box */
        tm_pal(k, 6, 0);
        pal_set(2, 1 + k * 16, rgb9(ink_rgb[k]));   /* sprites: the big letters' ink, offset k */
    }
    spr_patterns_from_pages(SPR_BANK * 2, 2, 0, SPR_NPAT, 10);
}

/* the panels' colour: the world's pen 2 (render.c palettes[]) */
void hud_world(uint8_t world)
{
    uint8_t k;
    uint16_t c = rgb9(ld_palettes[world][2]);
    for (k = 0; k < TX_INKS; k++) tm_pal(k, 3, c);
}

void hud_clear(void)
{
    uint8_t g;
    tm_clear();
    for (g = 0; g < BIG_GROUPS; g++) hud_big(g, 0, 0, "", 0, 0);
}

uint8_t hud_on_panel;

void hud_text(uint8_t col, uint8_t row, const char *s, uint8_t ink)
{
    tm_print(col, row, s, hud_on_panel ? T_PFONT : T_FONT, ink);
}

void hud_ctext(uint8_t row, const char *s, uint8_t ink)
{
    hud_text(20 - (uint8_t)(strlen(s) + 1) / 2, row, s, ink);
}

void hud_panel(uint8_t col, uint8_t row, uint8_t w, uint8_t h)
{
    tm_fill(col + 1, row + 1, w - 2, h - 2, T_PANEL, TX_GREY);
    tm_fill(col + 1, row, w - 2, 1, T_EDGE, TX_GREY);
    tm_fill(col + 1, row + h - 1, w - 2, 1, T_EDGE + 1, TX_GREY);
    tm_fill(col, row + 1, 1, h - 2, T_EDGE + 2, TX_GREY);
    tm_fill(col + w - 1, row + 1, 1, h - 2, T_EDGE + 3, TX_GREY);
    tm_put(col, row, T_EDGE + 4, TX_GREY);
    tm_put(col + w - 1, row, T_EDGE + 5, TX_GREY);
    tm_put(col, row + h - 1, T_EDGE + 6, TX_GREY);
    tm_put(col + w - 1, row + h - 1, T_EDGE + 7, TX_GREY);
}

/* sprite n: pattern, palette offset, at twice its size or not (5-byte attributes) */
static void spr_put(uint8_t n, int16_t x, int16_t y, uint8_t pat, uint8_t pal, uint8_t zoom)
{
    z80_outp(0x303B, n);
    z80_outp(0x57, (uint8_t)x);
    z80_outp(0x57, (uint8_t)y);
    z80_outp(0x57, (uint8_t)(pal << 4) | ((x >> 8) & 1));
    z80_outp(0x57, 0xC0 | pat);
    z80_outp(0x57, (zoom ? 0x0A : 0) | ((y >> 8) & 1));
}

static uint8_t big_shown[BIG_GROUPS] = { 0 };

void hud_big(uint8_t group, int16_t x, int16_t y, const char *s, uint8_t ink, uint8_t zoom)
{
    uint8_t n = 0, base = S_BIG + group * 24, i, step = zoom ? 32 : 16, sh = zoom ? 2 : 1;
    for (; *s && n < BIG_MAX; s++, x += step) {
        const char *p = strchr(SPR_GLYPHS, *s);
        if (!p || *s == ' ') continue;
        spr_put(base + n, x + sh, y + sh, PAT_GLYPH + (uint8_t)(p - SPR_GLYPHS), TX_BLACK, zoom);
        spr_put(base + 12 + n, x, y, PAT_GLYPH + (uint8_t)(p - SPR_GLYPHS), ink, zoom);
        n++;
    }
    for (i = n; i < big_shown[group]; i++) {
        spr_hide(base + i);
        spr_hide(base + 12 + i);
    }
    big_shown[group] = n;
}

void hud_big_centre(uint8_t group, int16_t y, const char *s, uint8_t ink, uint8_t zoom)
{
    hud_big(group, 160 - (int16_t)strlen(s) * (zoom ? 16 : 8), y, s, ink, zoom);
}

/* a gauge of 6 tiles at (col, row): k pixels of 48 filled (render.c's fuel bar) */
void hud_fuel(uint8_t col, uint8_t row, uint8_t k, uint8_t ink)
{
    uint8_t c;
    for (c = 0; c < 6; c++) {
        tm_put(col + c, row, T_FUEL_M + (k > 8 ? 8 : k), ink);
        k = k > 8 ? k - 8 : 0;
    }
}

void hud_strip(uint8_t row, uint8_t h)
{
    tm_fill(0, row, 40, h, T_PANEL, TX_GREY);
}
