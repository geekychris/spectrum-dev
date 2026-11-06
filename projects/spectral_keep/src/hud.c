/*
 * The HUD and the title, game over and victory screens (the original's
 * Hud.cs screens in main_os4.c), on the Next's tilemap: a 40 x 32 layer of
 * 8 x 8 tiles over the sprites and Layer 2, in the game's own font. Nothing
 * here touches the room's picture.
 *
 * port: the original draws text at any pixel; here text sits on 8-pixel
 * rows (the original's y + 8 rounded) and the drop-shadowed 2x title text
 * is tiles too. Keys on the screens are the Spectrum's.
 *
 * Tiles: 0 blank, 1.. the font (characters 32..127), a solid black tile,
 * the font inverted (characters 32..90, for highlighted text), 2x glyphs for
 * the big words. Palette offset k (0..7) is ink k: pen 1 the ink, pen 2 the
 * drop shadow, pen 3 black; offset 9 is white on red (the pause bar).
 */
#include <string.h>
#include <arch/zxn.h>
#include "keep.h"
#include "hud.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "palfx.h"

extern const unsigned char keep_font[96][8];

#define T_FONT  1
#define T_SOLID 97
#define T_INV   98                      /* characters 32..90 */
#define T_BIG   157
#define BIG_CHARS "SPECTRALKGMOVIYWDN!"
#define PAL_WHITE_ON_RED 9

static const uint16_t ink_rgb[8] = {    /* the original's pens 0..7 */
    RGB9(0, 0, 0), RGB9(1, 2, 7), RGB9(7, 1, 1), RGB9(7, 2, 7),
    RGB9(1, 6, 1), RGB9(1, 7, 7), RGB9(7, 7, 2), RGB9(7, 7, 7)
};

static uint8_t big_tile(char c)
{
    const char *p = strchr(BIG_CHARS, c);
    return p ? T_BIG + (uint8_t)(p - BIG_CHARS) * 4 : 0;
}

void hud_init(void)
{
    uint8_t k;
    const char *c;
    static const uint8_t solid[32] = {
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33
    };
    tm_init();
    tm_font(0, keep_font[0], 1, 1, 0);                 /* tile 0: a space, blank */
    tm_font(T_FONT, keep_font[0], 96, 1, 0);
    tm_def(T_SOLID, solid, 1);
    tm_font(T_INV, keep_font[0], 59, 3, 1);
    for (c = BIG_CHARS; *c; c++) tm_glyph2x(big_tile(*c), keep_font[*c - 32], 1, 2);
    for (k = 0; k < 8; k++) {
        tm_pal(k, 1, ink_rgb[k]);
        tm_pal(k, 2, RGB9(0, 0, 1));                   /* the original's shadow pen 0x101018 */
        tm_pal(k, 3, RGB9(0, 0, 0));
    }
    tm_pal(8, 1, RGB9(0, 0, 0));
    tm_pal(PAL_WHITE_ON_RED, 1, ink_rgb[INK_RED]);
    tm_pal(PAL_WHITE_ON_RED, 3, ink_rgb[INK_WHITE]);
}

/* ---- text ---- */

static void text(uint8_t col, uint8_t row, const char *s, uint8_t ink) { tm_print(col, row, s, T_FONT, ink); }
static void ctext(uint8_t row, const char *s, uint8_t ink) { text(20 - (uint8_t)(strlen(s) + 1) / 2, row, s, ink); }

static void inv(uint8_t col, uint8_t row, const char *s, uint8_t pal)
{
    for (; *s; s++, col++) tm_put(col, row, T_INV + (uint8_t)(*s - 32), pal);
}

static void big(uint8_t row, const char *s, uint8_t ink)
{
    uint8_t col = 20 - (uint8_t)strlen(s), t;
    for (; *s; s++, col += 2) {
        t = big_tile(*s);
        if (!t) continue;
        tm_put(col, row, t, ink);
        tm_put(col + 1, row, t + 1, ink);
        tm_put(col, row + 1, t + 2, ink);
        tm_put(col + 1, row + 1, t + 3, ink);
    }
}

static char *put_str(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_num(char *p, int16_t v)
{
    char t[6];
    uint8_t n = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

/* word wrap like Hud.Wrap; returns the number of lines */
static uint8_t wrap(const char *t, uint8_t width, char lines[][41], uint8_t max)
{
    uint8_t n = 0, len = 0;
    lines[0][0] = 0;
    while (*t && n < max) {
        const char *w = t;
        uint8_t wl = 0;
        while (w[wl] && w[wl] != ' ') wl++;
        if (len > 0 && len + 1 + wl > width) {
            if (++n >= max) break;
            lines[n][0] = 0;
            len = 0;
        }
        if (len > 0) lines[n][len++] = ' ';
        if (wl > width) wl = width;
        memcpy(lines[n] + len, w, wl);
        len += wl;
        lines[n][len] = 0;
        t = w + wl;
        while (*t == ' ') t++;
    }
    return len > 0 ? n + 1 : n;
}

static uint8_t msg_visible(void) { return G.clock < G.msg_until && G.message[0]; }   /* game_message_visible() */

static uint8_t blink(uint8_t per_s) { return ((G.clock * per_s * 2) / 50) % 2 == 0; }

/* ---- screens ---- */

static char lines[2][41];
static uint8_t bars;                    /* draw the screen's Layer 2 bars this time */

static void panel(void)
{
    char buf[41], *p;
    uint8_t n, i;
    if (!R.room) return;
    ctext(26, R.room->name, R.room->ink ? R.room->ink : INK_WHITE);
    put_num(put_str(buf, "{"), G.lives);
    text(1, 27, buf, INK_RED);
    p = put_num(put_str(buf, "|"), G.relics);
    put_num(put_str(p, "/"), G.level->relics);
    text(7, 27, buf, INK_YELLOW);
    put_num(put_str(buf, "}"), G.keys);
    text(16, 27, buf, INK_CYAN);
    p = put_num(put_str(buf, "ROOM "), G.nvisited);
    put_num(put_str(p, "/"), G.level->nrooms);
    text(22, 27, buf, INK_WHITE);
    if (msg_visible()) {
        n = wrap(G.message, 38, lines, 2);
        for (i = 0; i < n; i++) ctext(29 + i, lines[i], G.msg_ink);
    } else
        ctext(30, G.level->name, INK_WHITE);
}

/* port: the black bars are Layer 2 (index 128: the backdrop at half, black),
   so text on them needs no background, and the sprite clip window keeps the
   sprites off them */
#define L2_BLACK 128

static void sprite_clip(uint8_t y1)
{
    ZXN_WRITE_REG(0x1C, 0x02);                         /* reset the sprite clip index */
    ZXN_WRITE_REG(0x19, 0);
    ZXN_WRITE_REG(0x19, 159);                          /* x 0..319 (in pairs over the border) */
    ZXN_WRITE_REG(0x19, 0);
    ZXN_WRITE_REG(0x19, y1);
}

static void title(void)
{
    char buf[41], *p;
    uint8_t i;
    if (bars) {
        l2w_fill(0, 165, 320, 83, L2_BLACK);          /* the original's bar from y 157 */
        sprite_clip(164);
    }
    big(2, "SPECTRAL KEEP", INK_YELLOW);
    ctext(5, "AN ISOMETRIC ADVENTURE", INK_CYAN);
    for (i = 0; i < keep_nlevels && i < 4; i++) {
        uint8_t sel = i == G.selected, row = 21 + i;
        p = put_num(buf, i + 1);
        put_str(put_str(p, " "), keep_levels[i].name);
        text(7, row, sel ? ">" : " ", INK_WHITE);
        if (sel) inv(9, row, buf, (G.clock / 33) % 2 ? INK_CYAN : INK_YELLOW);
        else text(9, row, buf, INK_WHITE);
    }
    /* port: FIRE starts and T is music (M is fire on the Spectrum), V the tour (Q+E) */
    ctext(26, "FIRE PLAY   UP/DOWN CHOOSE A KEEP", INK_WHITE);
    p = put_str(put_str(buf, "C CONTROLS:"), G.grid_controls ? "GRID" : "SCREEN");
    put_str(put_str(p, "   T MUSIC:"), G.music_on ? "ON" : "OFF");
    ctext(28, buf, INK_GREEN);
    ctext(30, "(C) 2026 HITORRO", INK_MAGENTA);
}

static void tour(void)
{
    ctext(26, R.room->name, R.room->ink ? R.room->ink : INK_WHITE);
    ctext(28, G.level->name, INK_WHITE);
    ctext(30, "V OR FIRE: BACK TO THE TITLE", INK_GREEN);
}

static void end_screen(void)
{
    char buf[41];
    if (bars) l2w_fill(0, 0, 320, 256, L2_BLACK);
    if (G.state == GS_GAMEOVER) {
        big(11, "GAME OVER", INK_RED);
        put_num(put_str(buf, "RELICS FOUND: "), G.relics);
        ctext(17, buf, INK_YELLOW);
        put_num(put_str(buf, "ROOMS EXPLORED: "), G.nvisited);
        ctext(18, buf, INK_CYAN);
    } else {
        big(7, "VICTORY!", INK_YELLOW);
        ctext(13, "EVERY KEEP HAS FALLEN", INK_CYAN);
        ctext(14, "AND THE CROWN IS YOURS.", INK_CYAN);
        ctext(18, "THANKS FOR PLAYING", INK_MAGENTA);
    }
    if (blink(2)) ctext(23, "PRESS FIRE", INK_WHITE);
}

static void play(void)
{
    panel();
    if (G.state == GS_LEVELDONE && blink(3)) big(8, "WELL DONE!", INK_YELLOW);
    if (G.paused) {
        inv(15, 11, "  PAUSED  ", PAL_WHITE_ON_RED);
    }
}

/* everything the screens show, to redraw only on a change */
static struct {
    uint8_t state, tour, paused, selected, grid, music, lives, relics, keys, nvisited, ridx, level;
    uint8_t msg, blink2, blink3, sel_phase;
    const char *msg_text;
} now, shown;

uint8_t hud_update(void)
{
    memset(&now, 0, sizeof(now));
    now.state = G.state + 1;
    now.tour = G.tour;
    now.paused = G.paused;
    now.selected = G.selected;
    now.grid = G.grid_controls;
    now.music = G.music_on;
    now.lives = (uint8_t)G.lives;
    now.relics = (uint8_t)G.relics;
    now.keys = (uint8_t)G.keys;
    now.nvisited = G.nvisited;
    now.ridx = R.ridx;
    now.level = G.level_index;
    now.msg = msg_visible();
    if (G.msg_changed) { now.msg_text = 0; shown.msg_text = (const char *)1; G.msg_changed = 0; }
    if (G.state == GS_GAMEOVER || G.state == GS_VICTORY) now.blink2 = blink(2);
    if (G.state == GS_LEVELDONE) now.blink3 = blink(3);
    if (G.state == GS_TITLE) now.sel_phase = (G.clock / 33) % 2;
    if (!memcmp(&now, &shown, sizeof(now))) return 0;
    /* Layer 2 bars only when the kind of screen changes (they take a frame or two) */
    bars = now.state != shown.state || now.tour != shown.tour;
    shown = now;
    tm_clear();
    if (bars) sprite_clip(255);
    if (G.state == GS_TITLE) {
        if (G.tour) tour();
        else title();
    } else if (G.state == GS_GAMEOVER || G.state == GS_VICTORY) end_screen();
    else play();
    return 1;
}

void hud_reset(void) { memset(&shown, 0xFF, sizeof(shown)); }
