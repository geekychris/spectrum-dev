/*
 * hud.c - Orbit Docking's instruments and text (the original's render.c
 * hud(), titles and help pages) on the Next's other layers, over the view:
 *
 *   the tilemap: the original's 8 x 8 text (the Spectrum's ROM font with
 *     the original's drop shadow, in its pens), panels, the fuel gauge
 *   sprites: the axis box and its dot, the lives, the roll dial (its
 *     needle drawn into its patterns when it moves), and the original's
 *     16 x 16 font for the big text, each letter over its shadow
 *
 * Coordinates are the original's 320 x 256 screen: the display with its
 * border, Layer 2's view in the middle 256 x 192.
 * (Marble Tilt's hud.c, with this game's instruments.) In bank 24 with
 * screens.c.
 */
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "fixmath.h"
#include "fix16.h"
#include "hud.h"
#include "gfx_data.h"

#define T_FONT   1                       /* tiles 1..96: characters 32..127 */
#define T_PANEL  97                      /* pen 3 */
#define T_EDGE   98                      /* 98..105: a panel's border */
#define T_PFONT  112                     /* 112..207: the font on a panel */
#define T_FUEL_L 208                     /* the fuel gauge: its left tile with 0..7 pixels filled */
#define T_FUEL_M 216                     /* a middle tile, 0..8 */
#define T_FUEL_R 225                     /* its right edge */
#define FUEL_ROW 31                      /* port: the gauge on a tile row, 4 pixels lower */

#define S_DIAL   0                       /* sprites 0..8 the dial, 9..17 the axis box, 18 its dot, 19..21 lives */
#define S_AXIS   9
#define S_DOT    18
#define S_LIFE   19
#define S_BIG    24                      /* group g: shadows S_BIG + g * 24.., ink + 12 */
#define BIG_MAX  12
#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))


/* the original's pens (dock_palette: 1 white, 27 yellow, 26 red, 28
   green, 29 cyan, 3 grey, 0 black) in RGB333 */
static const uint16_t ink_rgb[7] = {
    RGB9(7, 7, 7), RGB9(7, 7, 2), RGB9(7, 1, 1), RGB9(3, 7, 3), RGB9(2, 7, 7), RGB9(4, 4, 5), RGB9(0, 0, 0)
};
#define RGB_PANEL RGB9(0, 1, 1)          /* 0x123 */
#define RGB_GREY  RGB9(4, 4, 5)          /* 0x99A */

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

/* ---- the dial (render.c hud(): two discs, a green mark at the top, the needle) ---- */
static uint8_t dial_base[2304] = { 0 }, dial_buf[2304] = { 0 };   /* 48 x 48 as 9 sprite patterns (in this bank) */
static int16_t dial_shown = -32768;

static void dial_px(uint8_t x, uint8_t y, uint8_t pen)
{
    if (x < 48 && y < 48) dial_buf[(((y >> 4) * 3 + (x >> 4)) << 8) + ((y & 15) << 4) + (x & 15)] = pen;
}

static void dial_init(void)
{
    uint8_t x, y;
    for (y = 0; y < 48; y++)
        for (x = 0; x < 48; x++) {
            int16_t dx = (int16_t)x - 24, dy = (int16_t)y - 24, d = dx * dx + dy * dy;
            dial_base[(((y >> 4) * 3 + (x >> 4)) << 8) + ((y & 15) << 4) + (x & 15)] =
                d <= 22 * 22 ? PAL_PANEL : d <= 24 * 24 ? PAL_GREY : 0xE3;
        }
    for (y = 2; y < 8; y++)
        for (x = 23; x < 26; x++) dial_base[(((y >> 4) * 3 + (x >> 4)) << 8) + ((y & 15) << 4) + (x & 15)] = PAL_GREEN;
}

void hud_dial(int16_t err, uint8_t ok)
{
    uint16_t a = (uint16_t)err * 2;          /* the slot repeats every half turn: shown a full turn */
    int16_t key = (int16_t)((a >> 9) | (ok ? 0x4000 : 0)), s, c, t;
    uint8_t k;
    if (key == dial_shown) return;
    dial_shown = key;
    dma_copy(dial_buf, dial_base, sizeof(dial_buf));
    a &= 0xFE00;                             /* 128 steps a turn */
    s = (int16_t)(a3_sin(a) >> 8);           /* Q8 */
    c = (int16_t)(a3_cos(a) >> 8);
    /* the needle: from the middle out 22, 3 wide at the middle narrowing to the tip */
    for (t = 0; t <= 22; t++) {
        int16_t w = (22 - t) * 3 / 22, j, ts = t * s, tc = t * c;
        for (j = -w; j <= w; j++) {
            int16_t px = 24 + ((ts + j * c) >> 8), py = 24 + ((j * s - tc) >> 8);
            dial_px((uint8_t)px, (uint8_t)py, ok ? PAL_GREEN : 1 + 16 * TX_YELLOW);
        }
    }
    for (k = 0; k < 9; k++) spr_pattern_dma(PAT_DIAL + k, dial_buf + ((uint16_t)k << 8));
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
    for (k = 0; k < 8; k++) fuel_tile(T_FUEL_L + k, 1, k, 0, 1);
    for (k = 0; k < 9; k++) fuel_tile(T_FUEL_M + k, 0, k, 8, 1);
    fuel_tile(T_FUEL_R, 0, 0, 0, 0);
    for (k = 0; k < 7; k++) {
        tm_pal(k, 1, ink_rgb[k]);
        tm_pal(k, 2, RGB9(0, 0, 0));
        tm_pal(k, 3, RGB_PANEL);
        tm_pal(k, 4, ink_rgb[k]);            /* a panel's border */
        tm_pal(k, 5, RGB_GREY);              /* the fuel gauge's box */
        tm_pal(k, 6, RGB9(0, 0, 0));
        pal_set(2, 1 + k * 16, ink_rgb[k]);  /* sprites: the big letters' ink, offset k */
    }
    pal_set(2, PAL_PANEL, RGB_PANEL);
    pal_set(2, PAL_GREY, RGB_GREY);
    pal_set(2, PAL_GREEN, ink_rgb[TX_GREEN]);
    pal_set(2, PAL_RED, ink_rgb[TX_RED]);
    pal_set(2, PAL_CYAN, ink_rgb[TX_CYAN]);
    spr_patterns_from_pages(SPR_BANK * 2, 2, 0, SPR_NPAT, 10);
    dial_init();
}

void hud_clear(void)
{
    uint8_t g;
    tm_clear();
    for (g = 0; g < BIG_GROUPS; g++) hud_big(g, 0, 0, "", 0, 0);
    hud_instruments_hide();
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

/* render.c hud(): the axis box at the top right, the craft's offset /6
   from its middle (292, 28); the lives; the dial at (160, 226) */
void hud_instruments(fix x, fix y, int8_t lives, int16_t roll_err, uint8_t roll_ok)
{
    uint8_t k;
    int16_t dx, dy;
    for (k = 0; k < 9; k++) spr_put(S_AXIS + k, 268 + (k % 3) * 16, 4 + (k / 3) * 16, PAT_AXIS + k, 0, 0);
    dx = 292 + (int16_t)((x >> 16) / 6);
    dy = 28 - (int16_t)((y >> 16) / 6);
    if (dx > 268 && dx < 314 && dy > 6 && dy < 50)
        spr_put(S_DOT, dx - 2, dy - 2, fhypot(x, y) < FROMI(14) ? PAT_DOT_OK : PAT_DOT_BAD, 0, 0);
    else spr_hide(S_DOT);
    for (k = 0; k < 3; k++)
        if (k < lives) spr_put(S_LIFE + k, 4 + k * 10, 15, PAT_LIFE, 0, 0);
        else spr_hide(S_LIFE + k);
    hud_dial(roll_err, roll_ok);
    for (k = 0; k < 9; k++) spr_put(S_DIAL + k, 136 + (k % 3) * 16, 202 + (k / 3) * 16, PAT_DIAL + k, 0, 0);
}

void hud_instruments_hide(void)
{
    uint8_t k;
    for (k = 0; k < 22; k++) spr_hide(k);
}

/* the fuel gauge (render.c hud(): a box from 40 to 120, k = fuel * 78 / max pixels from 41) */
void hud_fuel(uint8_t k, uint8_t ink)
{
    uint8_t c;
    tm_put(5, FUEL_ROW, T_FUEL_L + (k > 7 ? 7 : k), ink);
    k = k > 7 ? k - 7 : 0;
    for (c = 6; c < 15; c++) {
        tm_put(c, FUEL_ROW, T_FUEL_M + (k > 8 ? 8 : k), ink);
        k = k > 8 ? k - 8 : 0;
    }
    tm_put(15, FUEL_ROW, T_FUEL_R, ink);
}

/* sprites only on display lines 0..ybot (the clip window) */
void hud_clip(uint8_t ybot)
{
    ZXN_WRITE_REG(0x1C, 0x02);
    ZXN_WRITE_REG(0x19, 0);
    ZXN_WRITE_REG(0x19, 159);
    ZXN_WRITE_REG(0x19, 0);
    ZXN_WRITE_REG(0x19, ybot);
}
