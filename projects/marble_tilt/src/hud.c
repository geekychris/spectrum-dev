/*
 * hud.c - Marble Tilt's text and gauges (the original's hud(), titles and
 * help pages in render.c) on the Next's other layers, so the board on
 * Layer 2 is only drawn when it changes:
 *
 *   the tilemap (over everything): the original's 8 x 8 text, here in the
 *     Spectrum's ROM font with the original's drop shadow, in the
 *     original's pens; panels
 *   sprites at twice their size: the original's 16 x 16 font for its big
 *     text (the title, the clock, THROUGH!), each letter over its shadow
 *   sprites: the tilt gauge (render.c hud(): a ring, a cross, a dot)
 *
 * Coordinates are the original's 320 x 256 screen: the display with its
 * border, Layer 2's 256 x 192 board in the middle.
 */
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "hud.h"
#include "view_data.h"

#define T_FONT   1                       /* tiles 1..96: characters 32..127 */
#define T_PFONT  112                     /* 112..207: the same on a panel (pen 3 behind) */
#define T_PANEL  97                      /* pen 3 */
#define T_EDGE   98                      /* 99..105: the panel's border (top, bottom, left, right, corners) */
#define S_GAUGE  2                       /* sprites 2..10 the ring, 11 the dot */
#define S_BIG    16                      /* group g: shadows S_BIG + g * 24.., ink + 12 */
#define BIG_MAX  12
#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))

uint8_t rom_font[768];

/* the original's pens (tilt_palette: 1 white, 27 yellow, 26 red, 28
   green, 29 cyan, 3 grey, 0 black) in RGB333 */
static const uint16_t ink_rgb[7] = {
    RGB9(7, 7, 7), RGB9(7, 7, 2), RGB9(7, 1, 1), RGB9(3, 7, 3), RGB9(2, 6, 7), RGB9(4, 4, 5), RGB9(0, 0, 0)
};

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
    for (k = 0; k < 7; k++) {
        tm_pal(k, 1, ink_rgb[k]);
        tm_pal(k, 2, RGB9(0, 0, 0));
        tm_pal(k, 3, RGB9(0, 1, 1));         /* the panel, the original's 0x123 */
        tm_pal(k, 4, ink_rgb[k]);            /* a panel's border */
        /* sprites: big letters' ink, offset k */
        pal_set(2, SPR_INK + k * 16, ink_rgb[k]);
    }
    pal_set(2, SPR_RING, RGB9(0, 1, 1));
    pal_set(2, SPR_CROSS, RGB9(4, 4, 5));
    pal_set(2, SPR_DOT, ink_rgb[TX_YELLOW]);
    spr_patterns_from_pages(SPR_BANK * 2, 2, 0, 64, 10);
}

void hud_clear(void)
{
    uint8_t g;
    tm_clear();
    for (g = 0; g < BIG_GROUPS; g++) hud_big(g, 0, 0, "", 0);
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

static uint8_t big_shown[BIG_GROUPS];

void hud_big(uint8_t group, int16_t x, int16_t y, const char *s, uint8_t ink)
{
    uint8_t n = 0, base = S_BIG + group * 24, i;
    for (; *s && n < BIG_MAX; s++, x += 32) {
        const char *p = strchr(SPR_GLYPHS, *s);
        if (!p) continue;                    /* a space */
        spr_put(base + n, x + 2, y + 2, 11 + (uint8_t)(p - SPR_GLYPHS), TX_BLACK, 1);
        spr_put(base + 12 + n, x, y, 11 + (uint8_t)(p - SPR_GLYPHS), ink, 1);
        n++;
    }
    for (i = n; i < big_shown[group]; i++) {
        spr_hide(base + i);
        spr_hide(base + 12 + i);
    }
    big_shown[group] = n;
}

void hud_big_centre(uint8_t group, int16_t y, const char *s, uint8_t ink)
{
    hud_big(group, 160 - (int16_t)strlen(s) * 16, y, s, ink);
}

/* render.c hud(): the gauge's middle at (294, 26), the dot 15 pixels out at full tilt */
void hud_gauge(fix roll, fix pitch)
{
    uint8_t k;
    for (k = 0; k < 9; k++) spr_put(S_GAUGE + k, 294 - 24 + (k % 3) * 16, 26 - 24 + (k / 3) * 16, 1 + k, 0, 0);
    spr_put(S_GAUGE + 9, 294 - 8 + (int16_t)((roll * 15) / 7864L), 26 - 8 - (int16_t)((pitch * 15) / 7864L),
            10, 0, 0);
}

void hud_gauge_hide(void)
{
    uint8_t k;
    for (k = 0; k < 10; k++) spr_hide(S_GAUGE + k);
}
