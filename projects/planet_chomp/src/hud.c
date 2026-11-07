/*
 * The HUD, the title and the end screens on the tilemap (the original's
 * hud_play, hud_title and hud_over in main_68k.c), over everything. The
 * tilemap covers the whole 320 x 256 display, the original's 68k frame, so
 * the HUD keeps the original's layout, its words on 8-pixel rows (port:
 * the original's y + a few pixels); the 3D view is the 256 x 192 in the
 * middle. Bank 21 at 0x0000 (main.c's HUD()).
 *
 * Tiles: 32..95 the ROM font, 96..175 big letters (2 x 2 tiles, with a
 * drop shadow), then the radar's rings and the power bar (hud_tiles.c).
 * Each colour is a tilemap palette offset; pen 1 is the text, pen 2 the
 * big letters' shadow.
 */
#include <stdint.h>
#include <string.h>
#include "nexttile.h"
#include "palfx.h"
#include <arch/zxn.h>
#include "zxgame.h"
#include "pc.h"
#include "game.h"
#include "hud.h"

/* the original's pens (main_68k.c), as tilemap palette offsets */
enum { P_WHITE, P_DIM, P_YELLOW, P_GOLD, P_RED, P_SP1, P_SP2, P_SP3, P_SP4, P_RING, P_BAR, P_BARBLINK, P_COUNT };
static const uint16_t pen_rgb9[P_COUNT] = {
    RGB9(7, 7, 7), RGB9(4, 5, 6), RGB9(7, 6, 1), RGB9(7, 5, 1), RGB9(7, 2, 2),
    RGB9(5, 2, 7), RGB9(7, 6, 2), RGB9(6, 6, 7), RGB9(1, 6, 5),
    0, 0, 0
};
#define SHADOW RGB9(0, 1, 1)                /* the original's P_BAR_BG */

#define T_FONT 0                            /* tile = character (32..95) */
#define T_PRINT 32                          /* tm_print's first: ' ' is tile 32 */
#define T_BIG 96
#define T_RING 176
#define T_BAR (T_RING + RING_TILES)
#define FONT_ROM ((const uint8_t *)0x7000)  /* main.c copied the ROM font here (bank 5) */

#include "hud_tiles.h"

static const char big_chars[] = "ACDEGHILMNOPRSTUVWY!";

uint8_t hud_screen = 0xFF;                  /* what the tilemap shows now */
static int32_t shown_score = -1, shown_hi = -1;
static int16_t shown_crumbs = -1;
static uint8_t shown_level, shown_msg = 0xFF, shown_power = 0xFF, shown_blink = 0xFF;

static void put_num(uint8_t col, uint8_t row, int32_t v, uint8_t pal, uint8_t right)
{
    char buf[12];
    uint8_t n = 0, i;
    if (v < 0) v = 0;
    do { buf[n++] = '0' + (char)(v % 10); v /= 10; } while (v && n < 11);
    for (i = 0; i < n; i++) tm_put(right ? col - i : col + n - 1 - i, row, (uint8_t)buf[i], pal);
}

static uint8_t center_col(const char *s) { return (uint8_t)((TM_COLS - strlen(s)) / 2); }
static void ctext(uint8_t row, const char *s, uint8_t pal) { tm_print(center_col(s), row, s, T_PRINT, pal); }

/* big letters: 2 x 2 tiles each, centred */
static void big(uint8_t row, const char *s, uint8_t pal)
{
    uint8_t col = (uint8_t)((TM_COLS - 2 * strlen(s)) / 2);
    for (; *s; s++, col += 2) {
        const char *p = strchr(big_chars, *s);
        if (!p || *s == ' ') continue;
        {
            uint8_t t = T_BIG + (uint8_t)(p - big_chars) * 4;
            tm_put(col, row, t, pal);
            tm_put(col + 1, row, t + 1, pal);
            tm_put(col, row + 1, t + 2, pal);
            tm_put(col + 1, row + 1, t + 3, pal);
        }
    }
}

void hud_init(void)
{
    uint8_t i;
    tm_init();
    ZXN_WRITE_REG(0x4A, 0x00);                       /* the border (no layer there): black space */
    for (i = 0; i < P_COUNT; i++) {
        tm_pal(i, 0, 0);
        tm_pal(i, 1, pen_rgb9[i]);
        tm_pal(i, 2, SHADOW);
    }
    tm_pal(P_RING, 3, RGB9(2, 3, 7));                /* the original's P_RING, P_RINGDIM */
    tm_pal(P_RING, 4, RGB9(1, 2, 4));
    tm_pal(P_BAR, 5, RGB9(7, 6, 1));                 /* gold on the bar's background */
    tm_pal(P_BAR, 6, SHADOW);
    tm_pal(P_BARBLINK, 5, RGB9(7, 7, 7));
    tm_pal(P_BARBLINK, 6, SHADOW);
    tm_font(T_FONT + 32, FONT_ROM, 64, 1, 0);        /* ' ' .. '_' */
    for (i = 0; big_chars[i]; i++)
        tm_glyph2x(T_BIG + i * 4, FONT_ROM + (big_chars[i] - 32) * 8, 1, 2);
    tm_def(T_RING, ring_def, RING_TILES);
    tm_def(T_BAR, bar_def, 9);
    tm_clear();
}

static void ring(void)
{
    uint8_t i;
    for (i = 0; i < RING_CELLS; i++) tm_put(ring_cell[i * 3], ring_cell[i * 3 + 1], T_RING + ring_cell[i * 3 + 2], P_RING);
    tm_print(32, 21, "RADAR", T_PRINT, P_DIM);
}

/* ---- play ---- */

enum { MSG_NONE, MSG_READY, MSG_READY1, MSG_CLEAR, MSG_PAUSED };

static void play_static(void)
{
    tm_clear();
    tm_print(1, 1, "SCORE", T_PRINT, P_DIM);
    ctext(1, "HIGH SCORE", P_DIM);
    ring();
    if (g.demo) ctext(28, "DEMO - PRESS SPACE", P_DIM);
    shown_score = shown_hi = -1;
    shown_crumbs = -1;
    shown_level = 0;
    shown_msg = shown_power = shown_blink = 0xFF;
}

static void play_update(void)
{
    uint8_t msg = MSG_NONE, power = 0, blink = 0;
    if (g.score != shown_score) {
        tm_fill(1, 2, 10, 1, 0, 0);
        put_num(1, 2, g.score, P_WHITE, 0);
        shown_score = g.score;
    }
    if (g.hiscore != shown_hi) {
        tm_fill(15, 2, 10, 1, 0, 0);
        put_num(19, 2, g.hiscore, P_WHITE, 0);
        shown_hi = g.hiscore;
    }
    if (g.level != shown_level) {
        tm_fill(28, 1, 12, 1, 0, 0);
        tm_print(31, 1, "LEVEL", T_PRINT, P_YELLOW);
        put_num(38, 1, g.level, P_YELLOW, 1);
        shown_level = g.level;
    }
    if (g.crumbs_left != shown_crumbs) {
        tm_fill(28, 2, 12, 1, 0, 0);
        tm_print(29, 2, "CRUMBS", T_PRINT, P_DIM);
        put_num(38, 2, g.crumbs_left, P_DIM, 1);
        shown_crumbs = g.crumbs_left;
    }
    /* the power bar: 160 pixels, 20 tiles; blinking in its last 2 s */
    if (g.power_left > 0) {
        power = (uint8_t)(160L * g.power_left / (g.power_total ? g.power_total : 1));
        if (power == 0) power = 1;
        blink = g.power_left < TICKS(2) && (g.power_left % 20) < 10;
    }
    if (power != shown_power || blink != shown_blink) {
        if (!power) {
            tm_fill(10, 3, 20, 1, 0, 0);
            tm_fill(14, 4, 12, 1, 0, 0);
        } else {
            uint8_t i;
            for (i = 0; i < 20; i++) {
                uint8_t f = power >= (i + 1) * 8 ? 8 : power > i * 8 ? power - i * 8 : 0;
                tm_put(10 + i, 3, T_BAR + f, blink ? P_BARBLINK : P_BAR);
            }
            if (shown_power == 0 || shown_power == 0xFF) ctext(4, "INVINCIBLE!", P_GOLD);
        }
        shown_power = power;
        shown_blink = blink;
    }
    if (g.state == GS_READY) msg = g.level == 1 ? MSG_READY1 : MSG_READY;
    else if (g.state == GS_CLEAR) msg = MSG_CLEAR;
    else if (g.paused) msg = MSG_PAUSED;
    if (msg != shown_msg) {
        tm_fill(0, 10, TM_COLS, 2, 0, 0);
        tm_fill(0, 13, TM_COLS, 2, 0, 0);
        tm_fill(0, 18, TM_COLS, 2, 0, 0);
        tm_fill(0, 21, TM_COLS, 1, 0, 0);
        tm_print(32, 21, "RADAR", T_PRINT, P_DIM);
        if (msg == MSG_READY || msg == MSG_READY1) big(18, "READY!", P_YELLOW);
        if (msg == MSG_READY1) ctext(21, "EAT EVERY CRUMB - GRAB A KEY", P_DIM);
        if (msg == MSG_CLEAR) big(10, "PLANET CLEARED!", P_YELLOW);
        if (msg == MSG_PAUSED) big(13, "PAUSED", P_WHITE);
        shown_msg = msg;
    }
}

/* ---- the title (the original's, and the high scores in turn: port) ---- */

static void title_text(void)
{
    static const char *const names[4] = { "GLOOM", "SPARK", "MIST", "MOSS" };
    uint8_t i;
    tm_clear();
    big(4, "PLANET CHOMP", P_YELLOW);
    ctext(8, "A MAZE THAT WRAPS ALL THE WAY ROUND", P_DIM);
    for (i = 0; i < 4; i++) tm_print(4 + i * 9, 11, names[i], T_PRINT, P_SP1 + i);
    ctext(14, "EAT EVERY CRUMB ON THE PLANET.", P_WHITE);
    ctext(16, "GRAB A GOLDEN KEY TO TURN THE TABLES.", P_WHITE);
    ctext(18, "QAOP/CURSORS MOVE   Z/X SPIN THE VIEW", P_DIM);
    ctext(20, "C WHOLE PLANET   H PAUSE", P_DIM);
    tm_print(9, 29, "HIGH SCORE", T_PRINT, P_DIM);
    put_num(21, 29, g.hiscore, P_DIM, 0);
}

static void title_scores(void)
{
    uint8_t i;
    tm_clear();
    big(4, "PLANET CHOMP", P_YELLOW);
    ctext(9, "HIGH SCORES", P_YELLOW);
    for (i = 0; i < HS_ENTRIES; i++) {
        tm_put(12, 12 + i * 2, '1' + i, P_DIM);
        tm_print(15, 12 + i * 2, hs_table[i].name, T_PRINT, P_WHITE);
        put_num(27, 12 + i * 2, (int32_t)hs_table[i].score, P_WHITE, 1);
    }
}

static uint8_t title_page = 0xFF, title_blink = 0xFF;

static void title_update(void)
{
    uint8_t page = (uint8_t)((g.timer / TICKS(5)) & 1), blink = (g.clock / 25) % 3 != 2;
    if (page != title_page) {
        if (page) title_scores();
        else title_text();
        title_page = page;
        title_blink = 0xFF;
    }
    if (blink != title_blink) {
        tm_fill(0, 23, TM_COLS, 2, 0, 0);
        if (blink) big(23, "PRESS SPACE TO START", P_YELLOW);
        title_blink = blink;
    }
}

/* ---- game over, the initials ---- */

static void over_text(void)
{
    tm_clear();
    big(12, "GAME OVER", P_RED);
    ctext(16, "PRESS SPACE TO PLAY AGAIN", P_DIM);
    tm_print(7, 18, "SCORE", T_PRINT, P_WHITE);
    put_num(13, 18, g.score, P_WHITE, 0);
    tm_print(20, 18, "HIGH SCORE", T_PRINT, P_WHITE);
    put_num(31, 18, g.hiscore, P_WHITE, 0);
}

void hud_entry(const char *name, uint8_t pos, uint8_t place)
{
    uint8_t i;
    if (hud_screen != HUD_ENTRY) {
        tm_clear();
        big(8, "GAME OVER", P_RED);
        ctext(12, "A NEW HIGH SCORE!", P_YELLOW);
        tm_print(13, 14, "PLACE", T_PRINT, P_DIM);
        tm_put(19, 14, '1' + place, P_WHITE);
        put_num(22, 14, g.score, P_WHITE, 0);
        ctext(16, "TYPE YOUR INITIALS", P_DIM);
        ctext(17, "(OR UP/DOWN AND FIRE)", P_DIM);
        hud_screen = HUD_ENTRY;
    }
    for (i = 0; i < 3; i++) {
        uint8_t col = 16 + i * 3;
        tm_put(col, 20, (uint8_t)name[i], i == pos ? P_YELLOW : P_WHITE);
        tm_put(col, 21, i == pos ? '-' : ' ', P_YELLOW);
    }
}

void hud_update(void)
{
    uint8_t want = g.state == GS_TITLE ? HUD_TITLE : g.state == GS_OVER ? HUD_OVER : HUD_PLAY;
    if (want != hud_screen) {
        if (want == HUD_PLAY) play_static();
        if (want == HUD_OVER) over_text();
        if (want == HUD_TITLE) title_page = 0xFF;
        hud_screen = want;
    }
    if (want == HUD_PLAY) play_update();
    if (want == HUD_TITLE) title_update();
}
