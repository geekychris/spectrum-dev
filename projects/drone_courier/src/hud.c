/*
 * hud.c - Drone Courier's HUD, titles and help pages (the original's
 * render.c hud(), render_title(), render_help(), render_game()'s boxes)
 * on the Next's other layers, so Layer 2 holds only the 3D view:
 *
 *   the tilemap (over Layer 2, under the sprites): the original's 8 x 8
 *     text in the Spectrum's ROM font with its drop shadow, in its pens;
 *     panels; the battery bar
 *   sprites: the big text (the engine's 16 x 16 font, each letter over its
 *     shadow), the arrow to the next pad (16 headings), the wind gauge
 *
 * Coordinates are the original's 320 x 256 screen, which is the Next's
 * display with its border; the 3D view (Layer 2, 256 x 192) is in the middle.
 * (port: the same layout as Marble Tilt's hud.c, another amiga3d game)
 */
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "hud.h"
#include "gfx_data.h"
#include "drone_data.h"
#include "view.h"

#define T_FONT   1                       /* tiles 1..96: characters 32..127 */
#define T_PANEL  97                      /* pen 3 */
#define T_EDGE   98                      /* 98..105: the panel's border (top, bottom, left, right, corners) */
#define T_BAR    106                     /* 106..114: the battery bar, 0..8 pixels filled */
#define T_PFONT  128                     /* 128..223: the font on a panel (pen 3 behind) */
#define S_BIG    16                      /* group g: shadows S_BIG + g * 24.., ink + 12 */
#define BIG_MAX  12
#define S_DRONE  0                       /* 0..9: the drone, 4-bit patterns 112..121 */
#define P4_DRONE 112
#define PAL_DRONE 12                     /* sprite palette offsets 12 (a parcel), 13 (fragile) */
#define S_ARROW  10
#define S_WIND   11
#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))

#define rom_font ((const uint8_t *)0x3D00)    /* the ROM's characters 32..127 (hud_init runs with the ROM in) */

static const uint16_t ink_rgb[TX_INKS] = {
    RGB9(7, 7, 7), RGB9(7, 7, 2), RGB9(7, 1, 1), RGB9(3, 7, 3), RGB9(2, 6, 7), RGB9(4, 4, 5), RGB9(0, 0, 0),
    RGB9(1, 4, 7), RGB9(1, 6, 3)
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

/* render.c: g_box(cv, 40, H-12, 122, H-4, 0, 3) and the bar inside it: tile
   k has its first k pixels filled (pen 1, the ink), a grey frame (pen 5)
   on rows 0 and 7, black (pen 6) between */
static void bar_tiles(void)
{
    uint8_t def[32], k, y, x;
    for (k = 0; k <= 8; k++) {
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x += 2) {
                uint8_t a, b;
                if (y == 0 || y == 7) a = b = 5;
                else { a = x < k ? 1 : 6; b = x + 1 < k ? 1 : 6; }
                def[y * 4 + x / 2] = a << 4 | b;
            }
        tm_def(T_BAR + k, def, 1);
    }
}

void hud_init(void)
{
    uint8_t k;
    static const uint8_t edges[8][8] = {
        { 0xFF, 0xFF, 0, 0, 0, 0, 0, 0 },                    /* top */
        { 0, 0, 0, 0, 0, 0, 0xFF, 0xFF },                    /* bottom */
        { 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0 },  /* left */
        { 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 },  /* right */
        { 0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0 },  /* top left */
        { 0xFF, 0xFF, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 },  /* top right */
        { 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF },  /* bottom left */
        { 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0xFF, 0xFF },  /* bottom right */
    };
    tm_init();
    for (k = 0; k < 96; k++) {
        shadow_tile(T_FONT + k, rom_font + k * 8, 0);
        shadow_tile(T_PFONT + k, rom_font + k * 8, 3);
    }
    fill_tile(T_PANEL, 3);
    for (k = 0; k < 8; k++) tm_font(T_EDGE + k, edges[k], 1, 4, 3);
    bar_tiles();
    for (k = 0; k < TX_INKS; k++) {
        tm_pal(k, 1, ink_rgb[k]);
        tm_pal(k, 2, RGB9(0, 0, 0));
        tm_pal(k, 3, RGB9(0, 1, 1));         /* the panel, the original's 0x123 */
        tm_pal(k, 4, ink_rgb[k]);            /* a panel's border */
        tm_pal(k, 5, RGB9(4, 4, 5));         /* the battery's frame, 0x99A */
        tm_pal(k, 6, RGB9(0, 0, 0));
        pal_set(2, SPR_INK + k * 16, ink_rgb[k]);
    }
    spr_patterns_from_pages(SPR_BANK * 2, 2, 0, SPR_NPAT, 10);
    /* the drone: render.c's pens 17, 18, 19, 20 (21 fragile) */
    for (k = 0; k < 2; k++) {
        pal_set(2, (PAL_DRONE + k) * 16 + 1, RGB9(7, 7, 7));
        pal_set(2, (PAL_DRONE + k) * 16 + 2, RGB9(4, 4, 4));
        pal_set(2, (PAL_DRONE + k) * 16 + 4, RGB9(2, 2, 2));
    }
    pal_set(2, PAL_DRONE * 16 + 5, RGB9(5, 4, 2));          /* 0xB85 */
    pal_set(2, (PAL_DRONE + 1) * 16 + 5, RGB9(7, 2, 2));    /* 0xE44 */
}

/* the drone's frame into patterns 112..121 (from its page, through slot 2),
   then its 10 sprites; only a new frame is copied */
static uint8_t drone_frame = 255, drone_shown;

void hud_drone(void)
{
    const DroneSpr *d = &view_drone;
    uint8_t k, saved, attr2, n;
    if (!d->show) { hud_drone_hide(); return; }
    if (d->frame != drone_frame) {
        saved = ZXN_READ_REG(0x52);
        ZXN_WRITE_REG(0x52, DRONE_PAGE + d->frame / 6);
        z80_outp(0x303B, P4_DRONE / 2);
        dma_to_port(0x5B, (const void *)(0x4000 + (d->frame % 6) * DRONE_FRAME), DRONE_FRAME);
        ZXN_WRITE_REG(0x52, saved);
        drone_frame = d->frame;
    }
    attr2 = (uint8_t)((PAL_DRONE + d->fragile) << 4) | (d->mirror ? 0x08 : 0);
    for (k = 0; k < 10; k++) {
        uint8_t col = k % 5, row = k / 5;
        int16_t x = d->x + (d->mirror ? 4 - col : col) * 16, y = d->y + row * 16;
        n = P4_DRONE + k;
        z80_outp(0x303B, S_DRONE + k);
        z80_outp(0x57, (uint8_t)x);
        z80_outp(0x57, (uint8_t)y);
        z80_outp(0x57, attr2 | ((x >> 8) & 1));
        z80_outp(0x57, 0xC0 | (n >> 1));
        z80_outp(0x57, 0x80 | ((n & 1) << 6) | ((y >> 8) & 1));
    }
    drone_shown = 1;
}

void hud_drone_hide(void)
{
    uint8_t k;
    if (!drone_shown) return;
    for (k = 0; k < 10; k++) spr_hide(S_DRONE + k);
    drone_shown = 0;
}

void hud_clear(void)
{
    uint8_t g;
    tm_clear();
    for (g = 0; g < BIG_GROUPS; g++) hud_big(g, 0, 0, "", 0, 2);
    hud_arrow_hide();
    hud_wind_hide();
    hud_drone_hide();
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

void hud_blank(uint8_t col, uint8_t row, uint8_t n)
{
    tm_fill(col, row, n, 1, hud_on_panel ? T_PANEL : 0, TX_GREY);
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

void hud_fill_panel(void)
{
    tm_fill(0, 0, TM_COLS, TM_ROWS, T_PANEL, TX_GREY);
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

void hud_big(uint8_t group, int16_t x, int16_t y, const char *s, uint8_t ink, uint8_t scale)
{
    uint8_t n = 0, base = S_BIG + group * 24, i, zoom = scale > 2;
    int16_t step = zoom ? 32 : 16, sh = zoom ? 2 : 1;
    for (; *s && n < BIG_MAX; s++, x += step) {
        const char *p = strchr(SPR_GLYPHS, *s);
        if (!p || *s == ' ') continue;
        spr_put(base + n, x + sh, y + sh, P_GLYPH + (uint8_t)(p - SPR_GLYPHS), TX_BLACK, zoom);
        spr_put(base + 12 + n, x, y, P_GLYPH + (uint8_t)(p - SPR_GLYPHS), ink, zoom);
        n++;
    }
    for (i = n; i < big_shown[group]; i++) {
        spr_hide(base + i);
        spr_hide(base + 12 + i);
    }
    big_shown[group] = n;
}

void hud_big_centre(uint8_t group, int16_t y, const char *s, uint8_t ink, uint8_t scale)
{
    hud_big(group, 160 - (int16_t)strlen(s) * (scale > 2 ? 16 : 8), y, s, ink, scale);
}

/* render.c: the arrow's middle at (160, 22), its tip 14 out; port: one of
   16 headings, a 16 x 16 pattern at twice its size */
void hud_arrow(uint16_t rel, uint8_t ink)
{
    spr_put(S_ARROW, 160 - 16, 22 - 16, P_ARROW + (uint8_t)(((rel + 2048) >> 12) & 15), ink, 1);
}

void hud_arrow_hide(void) { spr_hide(S_ARROW); }

/* render.c: a cyan line wx long across from (290, H-9) and one wz long up
   from it, 2 pixels thick (they cover its white dot); drawn into one 16 x 16
   pattern with (290, H-9) at (8, 8) */
static int16_t wind_last_x = 999, wind_last_z;

void hud_wind(int16_t wx, int16_t wz)
{
    static uint8_t pat[256];
    int16_t x, y;
    if (wx != wind_last_x || wz != wind_last_z) {
        if (wx > 7) wx = 7;
        if (wx < -8) wx = -8;
        if (wz > 8) wz = 8;
        if (wz < -7) wz = -7;
        memset(pat, L2_TRANSPARENT, sizeof(pat));
        for (x = (wx > 0 ? 0 : wx); x <= (wx > 0 ? wx : 0); x++) {
            pat[8 * 16 + 8 + x] = SPR_INK;
            pat[9 * 16 + 8 + x] = SPR_INK;
        }
        for (y = (wz > 0 ? -wz : 0); y <= (wz > 0 ? 0 : -wz); y++) {
            pat[(8 + y) * 16 + 7] = SPR_INK;
            pat[(8 + y) * 16 + 8] = SPR_INK;
        }
        spr_pattern(P_WIND, pat);
        wind_last_x = wx;
        wind_last_z = wz;
    }
    spr_put(S_WIND, 290 - 8, 256 - 9 - 8, P_WIND, TX_CYAN, 0);
}

void hud_wind_hide(void) { spr_hide(S_WIND); wind_last_x = 999; }

/* the bar: 10 cells from x 40 on the bottom row */
void hud_battery(uint8_t w)
{
    uint8_t k, ink = w > 20 ? TX_GREEN : TX_RED;
    for (k = 0; k < 10; k++) {
        uint8_t f = w >= 8 ? 8 : w;
        tm_put(5 + k, 30, T_BAR + f, ink);
        w -= f;
    }
}
