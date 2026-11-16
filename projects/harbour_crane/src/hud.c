/*
 * hud.c - Harbour Crane's text and gauges (the original's hud(),
 * render_game()'s messages, render_title() and render_help()) on the Next's
 * tilemap, over the picture, so nothing is drawn into Layer 2:
 *
 *   text in the Spectrum's ROM font with the original's drop shadow, in
 *     the original's pens; panels (the original's g_box) with their border
 *   big text (the original's 2x and 3x words) as 2x tiles, defined for the
 *     letters on screen when it changes
 *   the LIST gauge's bar as tiles, its needles and the wind arrow as sprites
 *
 * Positions follow the original's 320 x 256 screen: the 256 x 192 view is
 * in the middle (Layer 2 at 32, 32), the score line in the top border and
 * the gauges in the bottom one.
 */
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "hud.h"
#include "gfx_data.h"

#define T_FONT   1                       /* 1..96: characters 32..127, on the picture */
#define T_PFONT  97                      /* 97..192: the same on a panel */
#define T_PANEL  193
#define T_EDGE   194                     /* 194..201: the panel's border */
#define T_GAUGE  202                     /* 202..205: the gauge's bar */
#define T_BIG    206                     /* 206..253: up to 12 big letters */
#define BIG_MAX  12
#define S_HUD    100                     /* sprites: 100 the needle, 101 the list mark, 102..105 the wind */

uint8_t rom_font[768];

/* the original's pens: 1 white, 27 yellow, 26 red, 28 green, 29 cyan, 3
   grey, 10 the crane's yellow (the title) */
static const uint8_t ink_pen[TX_COUNT] = {1, 27, 26, 28, 29, 3, 10};

/* a 4-bit tile from an 8 x 8 glyph: pen 1 the glyph, pen 2 its shadow a
   pixel down and right (g_text_shadow), the rest bg */
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

/* the sprites: 4-bit patterns after the crane's, palette offset 14 */
#define SPR_HUD_PAL 14
#define P_NEEDLE (CRANE_PATTERNS)
#define P_MARK   (CRANE_PATTERNS + 1)
#define P_SHAFT  (CRANE_PATTERNS + 2)
#define P_HEADR  (CRANE_PATTERNS + 3)
#define P_HEADL  (CRANE_PATTERNS + 4)

static void hud_patterns(void)
{
    static uint8_t p[128];
    uint8_t k;
    /* the needle: pen 1 (white), a pixel wide, 11 high */
    memset(p, 0, 128);
    for (k = 0; k < 11; k++) p[k * 8] = 0x10;
    z80_outp(0x303B, P_NEEDLE >> 1 | (P_NEEDLE & 1) << 7);
    z80_otir(p, 0x5B, 128);
    /* the list mark: pen 2 (yellow), 3 x 5 */
    memset(p, 0, 128);
    for (k = 0; k < 5; k++) {
        p[k * 8] = 0x22;
        p[k * 8 + 1] = 0x20;
    }
    z80_outp(0x303B, P_MARK >> 1 | (P_MARK & 1) << 7);
    z80_otir(p, 0x5B, 128);
    /* the wind's shaft: pen 3 (cyan), 16 x 2 at rows 2-3; the heads 3 x 6 */
    memset(p, 0, 128);
    memset(p + 16, 0x33, 16);
    z80_outp(0x303B, P_SHAFT >> 1 | (P_SHAFT & 1) << 7);
    z80_otir(p, 0x5B, 128);
    memset(p, 0, 128);
    for (k = 0; k < 6; k++) p[k * 8] = 0x33, p[k * 8 + 1] = 0x30;
    z80_outp(0x303B, P_HEADR >> 1 | (P_HEADR & 1) << 7);
    z80_otir(p, 0x5B, 128);
    z80_outp(0x303B, P_HEADL >> 1 | (P_HEADL & 1) << 7);
    z80_otir(p, 0x5B, 128);
    pal_set(2, SPR_HUD_PAL * 16 + 1, l2_pal[PEN0 + 1]);
    pal_set(2, SPR_HUD_PAL * 16 + 2, l2_pal[PEN0 + 27]);
    pal_set(2, SPR_HUD_PAL * 16 + 3, l2_pal[PEN0 + 29]);
}

static void spr4(uint8_t n, int16_t x, int16_t y, uint8_t pat, uint8_t show)
{
    static uint8_t a[5];
    a[0] = (uint8_t)x;
    a[1] = (uint8_t)y;
    a[2] = (SPR_HUD_PAL << 4) | ((x >> 8) & 1);
    a[3] = (show ? 0xC0 : 0x40) | (pat >> 1);
    a[4] = 0x80 | ((pat & 1) << 6) | ((y >> 8) & 1);
    z80_outp(0x303B, n);
    z80_otir(a, 0x57, 5);
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
    /* the gauge's bar (gauge(): a grey border, black inside, red ends): 8
       pixels high, the border its top and bottom rows */
    static const uint8_t gauge[4][32] = {
        { 0x55, 0x55, 0x55, 0x55, 0x56, 0x66, 0x66, 0x66, 0x56, 0x66, 0x66, 0x66, 0x56, 0x66, 0x66, 0x66,
          0x56, 0x66, 0x66, 0x66, 0x56, 0x66, 0x66, 0x66, 0x56, 0x66, 0x66, 0x66, 0x55, 0x55, 0x55, 0x55 },
        { 0x55, 0x55, 0x55, 0x55, 0x66, 0x77, 0x77, 0x77, 0x66, 0x77, 0x77, 0x77, 0x66, 0x77, 0x77, 0x77,
          0x66, 0x77, 0x77, 0x77, 0x66, 0x77, 0x77, 0x77, 0x66, 0x77, 0x77, 0x77, 0x55, 0x55, 0x55, 0x55 },
        { 0x55, 0x55, 0x55, 0x55, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,
          0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x55, 0x55, 0x55, 0x55 },
        { 0x55, 0x55, 0x55, 0x55, 0x77, 0x77, 0x77, 0x66, 0x77, 0x77, 0x77, 0x66, 0x77, 0x77, 0x77, 0x66,
          0x77, 0x77, 0x77, 0x66, 0x77, 0x77, 0x77, 0x66, 0x77, 0x77, 0x77, 0x66, 0x55, 0x55, 0x55, 0x55 },
    };
    tm_init();
    for (k = 0; k < 96; k++) {
        shadow_tile(T_FONT + k, rom_font + k * 8, 0);
        shadow_tile(T_PFONT + k, rom_font + k * 8, 3);
    }
    fill_tile(T_PANEL, 3);
    for (k = 0; k < 8; k++) tm_font(T_EDGE + k, edges[k], 1, 4, 3);
    tm_def(T_GAUGE, gauge[0], 4);
    for (k = 0; k < TX_COUNT; k++) {
        tm_pal(k, 1, l2_pal[PEN0 + ink_pen[k]]);
        tm_pal(k, 2, l2_pal[PEN0 + 0]);          /* the shadow: black */
        tm_pal(k, 3, l2_pal[PEN0 + 2]);          /* the panel: pen 2 */
        tm_pal(k, 4, l2_pal[PEN0 + ink_pen[k]]);  /* the panel's border */
        tm_pal(k, 5, l2_pal[PEN0 + 3]);          /* the gauge: grey border, */
        tm_pal(k, 6, l2_pal[PEN0 + 26]);         /* red ends, */
        tm_pal(k, 7, l2_pal[PEN0 + 0]);          /* black */
    }
    hud_patterns();
    hud_gauge_hide();
}

void hud_clear(void) { tm_clear(); }

uint8_t hud_on_panel;

void hud_text(uint8_t col, uint8_t row, const char *s, uint8_t ink)
{
    tm_print(col, row, s, hud_on_panel ? T_PFONT : T_FONT, ink);
}

void hud_ctext(uint8_t row, const char *s, uint8_t ink)
{
    hud_text((uint8_t)((40 - strlen(s)) / 2), row, s, ink);
}

void hud_panel(uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t ink)
{
    uint8_t r;
    tm_fill(col + 1, row + 1, w - 2, h - 2, T_PANEL, ink);
    tm_fill(col + 1, row, w - 2, 1, T_EDGE + 0, ink);
    tm_fill(col + 1, row + h - 1, w - 2, 1, T_EDGE + 1, ink);
    for (r = row + 1; r < row + h - 1; r++) {
        tm_put(col, r, T_EDGE + 2, ink);
        tm_put(col + w - 1, r, T_EDGE + 3, ink);
    }
    tm_put(col, row, T_EDGE + 4, ink);
    tm_put(col + w - 1, row, T_EDGE + 5, ink);
    tm_put(col, row + h - 1, T_EDGE + 6, ink);
    tm_put(col + w - 1, row + h - 1, T_EDGE + 7, ink);
}

/* big letters: 2 x 2 tiles each, defined for the letters of the words
   on screen (big_begin, then big_word for each word) */
static char big_chars[BIG_MAX + 1];
static uint8_t big_n;

void hud_big_begin(void)
{
    big_n = 0;
    big_chars[0] = 0;
}

static uint8_t big_tile(char c)
{
    const char *p = strchr(big_chars, c);
    if (!p) {
        if (big_n >= BIG_MAX) return 0;
        big_chars[big_n] = c;
        big_chars[big_n + 1] = 0;
        tm_glyph2x(T_BIG + big_n * 4, rom_font + (c - 32) * 8, 1, 2);
        return T_BIG + 4 * big_n++;
    }
    return T_BIG + 4 * (uint8_t)(p - big_chars);
}

void hud_big(uint8_t row, const char *s, uint8_t ink)
{
    uint8_t col = (uint8_t)(20 - strlen(s)), t;
    for (; *s; s++, col += 2) {
        if (*s == ' ') continue;
        t = big_tile(*s);
        tm_put(col, row, t, ink);
        tm_put(col + 1, row, t + 1, ink);
        tm_put(col, row + 1, t + 2, ink);
        tm_put(col + 1, row + 1, t + 3, ink);
    }
}

/* the LIST gauge (render.c gauge()): the bar at x 40 (tiles 5..17, 104
   pixels: port, the original's is 100) on the bottom line; the white
   needle is the heel, the yellow mark the list */
#define GAUGE_X  40
#define GAUGE_Y  232
#define GAUGE_W  104

void hud_gauge_bar(void)
{
    uint8_t c;
    tm_put(5, 29, T_GAUGE, TX_WHITE);
    tm_put(6, 29, T_GAUGE + 1, TX_WHITE);
    for (c = 7; c < 16; c++) tm_put(c, 29, T_GAUGE + 2, TX_WHITE);
    tm_put(16, 29, T_GAUGE + 3, TX_WHITE);
    tm_put(17, 29, T_GAUGE + 0, TX_WHITE);
}

static int16_t gauge_pos(fix v)
{
    int16_t n = (int16_t)(fdiv(v, HC_CAPSIZE) * (GAUGE_W / 2) >> 16);
    if (n < -GAUGE_W / 2 + 1) n = -GAUGE_W / 2 + 1;
    if (n > GAUGE_W / 2 - 1) n = GAUGE_W / 2 - 1;
    return n;
}

void hud_gauge(fix heel, fix list, int16_t wind)
{
    int16_t n = gauge_pos(heel), l = gauge_pos(list), len, x0 = 220;
    uint8_t k;
    spr4(S_HUD + 1, GAUGE_X + GAUGE_W / 2 + l - 1, GAUGE_Y + 2, P_MARK, 1);
    spr4(S_HUD, GAUGE_X + GAUGE_W / 2 + n, GAUGE_Y - 2, P_NEEDLE, 1);
    /* the wind: an arrow from x 220, |wind| long (40 at most) */
    len = wind < 0 ? -wind : wind;
    if (len > 40) len = 40;
    for (k = 0; k < 3; k++) {
        int16_t sx = wind > 0 ? x0 + k * 16 : x0 + len - 16 - k * 16;
        uint8_t show = len && k * 16 < len;
        if (wind > 0 && sx + 16 > x0 + len) sx = x0 + len - 16;
        if (wind < 0 && sx < x0) sx = x0;
        spr4(S_HUD + 2 + k, sx, GAUGE_Y + 1, P_SHAFT, show);
    }
    spr4(S_HUD + 5, wind > 0 ? x0 + len - 2 : x0, GAUGE_Y + 1, wind > 0 ? P_HEADR : P_HEADL, len != 0);
}

void hud_gauge_hide(void)
{
    uint8_t k;
    for (k = 0; k < 6; k++) spr4(S_HUD + k, 0, 0, P_NEEDLE, 0);
}
