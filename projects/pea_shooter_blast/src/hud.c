/*
 * hud.c - Pea Shooter Blast's text on the Next's tilemap (the original's
 * draw_hud, draw_title's words, draw_gameover and draw_levelclear), in the
 * original's 5x7 font, over the scrolling Layer 2 and the sprites: the text
 * stays put while the playfield scrolls under it, and nothing is drawn into
 * the playfield. The tmtext module (sdk) rewrites a line only when it
 * changes; big words are the font at 2x-4x.
 *
 * port: text sits on 8-pixel rows and columns (the original places it
 * anywhere): the HUD's rows 2 and 12 are rows 0 and 1, the title's lines
 * the nearest rows. The health bars and the lives are characters of their
 * own ('|' an empty bar, 127 a full one, '~' a tank).
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "nexttile.h"
#include "tmtext.h"
#include "palfx.h"
#include "psb.h"
#include "view.h"

#define T_FONT 1                        /* tiles 1-96: characters 32..127 */
#define T_POOL 97                       /* big letters */

/* tilemap palettes: pen 1 the colour, pen 2 the shadow, pen 3 an empty bar
   (main.c's colours 1, 8, 10, 3, 4, 2, 7) */
enum { P_WHITE, P_RED, P_YELLOW, P_LGREEN, P_LIME, P_GREEN, P_GREY };
static const uint16_t pal_rgb[7] = {
    RGB9(7, 7, 7), RGB9(7, 0, 0), RGB9(7, 7, 0), RGB9(0, 6, 0), RGB9(0, 7, 0), RGB9(0, 5, 0), RGB9(5, 5, 5)
};

extern const uint8_t font_5x7[96][7];
static uint8_t font8[96][8];            /* the font as 8x8 glyphs: a column left, a row below */

uint8_t hud_title_page;

enum { S_HP, S_BARS, S_GUN, S_GUNLV, S_SCORE, S_LIVES, S_LV, S_BIG1, S_BIG2, S_L1, S_L2, S_L3, S_L4, S_L5, S_L6,
       S_L7, S_COUNT };
static tmt_slot slots[S_COUNT];
static char buf[44];

static const char *num(int32_t v) { tmt_num(buf, v); return buf; }

/* draw_hud */
static void hud(const GameState *gs)
{
    uint8_t i, n;
    char *d = buf;
    tmt_say(S_HP, 0, 0, "HP", P_WHITE, 1, 1);
    for (i = 0; i < MAX_HEALTH; i++) *d++ = i < gs->tank.health ? 127 : '|';
    *d = 0;
    tmt_say(S_BARS, 3, 0, buf, gs->tank.health <= 2 ? P_RED : P_LIME, 1, 0);
    tmt_say(S_GUN, 17, 0, "GUN", P_WHITE, 1, 1);
    buf[0] = (char)('1' + gs->tank.weapon_level);
    buf[1] = 0;
    tmt_say(S_GUNLV, 21, 0, buf, P_YELLOW, 1, 1);
    num(gs->score);
    tmt_say(S_SCORE, (uint8_t)(40 - strlen(buf)), 0, buf, P_WHITE, 1, 1);
    n = gs->lives - 1 > 5 ? 5 : (uint8_t)(gs->lives > 0 ? gs->lives - 1 : 0);
    for (i = 0, d = buf; i < n; i++) { *d++ = '~'; *d++ = ' '; }
    *d = 0;
    tmt_say(S_LIVES, 0, 1, buf, P_GREEN, 1, 0);
    buf[0] = 'L'; buf[1] = 'V'; buf[2] = (char)('1' + gs->level); buf[3] = 0;
    tmt_say(S_LV, 18, 1, buf, P_WHITE, 1, 1);
}

static void overlays(const GameState *gs)
{
    if (gs->state == STATE_GAMEOVER) {                 /* draw_gameover */
        tmt_say(S_BIG1, TMT_CENTRE, 12, "GAME OVER", P_RED, 3, 0);
        tmt_say(S_BIG2, TMT_CENTRE, 17, num(gs->score), P_YELLOW, 2, 0);
    } else if (gs->state == STATE_LEVELCLEAR) {        /* draw_levelclear */
        tmt_say(S_BIG1, TMT_CENTRE, 13, "LEVEL CLEAR!", P_LIME, 3, 0);
        memcpy(buf, "LEVEL 1", 8);
        buf[6] = (char)('1' + gs->level);
        tmt_say(S_BIG2, TMT_CENTRE, 18, buf, P_WHITE, 2, 0);
    } else {
        tmt_quiet(S_BIG1);
        tmt_quiet(S_BIG2);
    }
}

/* draw_title's smaller words; port: the Spectrum's keys, and every other 5 s the
   high-score table instead of the controls */
static void title(const GameState *gs)
{
    /* PEA SHOOTER and BLAST are drawn into Layer 2 (view_title) */
    tmt_say(S_GUN, TMT_CENTRE, 12, "PRESS FIRE TO START", P_LGREEN, 1, 1);
    if (!hud_title_page) {
        tmt_say(S_BIG1, TMT_CENTRE, 19, "CONTROLS", P_WHITE, 2, 1);
        tmt_say(S_L1, 5, 22, "QAOP/CURSORS/JOY  MOVE", P_LGREEN, 1, 1);
        tmt_say(S_L2, 5, 23, "Q/UP/JOY UP       JUMP, CLIMB", P_LGREEN, 1, 1);
        tmt_say(S_L3, 5, 24, "SPACE/FIRE        SHOOT", P_LGREEN, 1, 1);
        tmt_say(S_L4, 5, 25, "BREAK             TITLE", P_LGREEN, 1, 1);
        tmt_quiet(S_L5);
    } else {
        uint8_t k;
        tmt_say(S_BIG1, TMT_CENTRE, 19, "HIGH SCORES", P_WHITE, 2, 1);
        for (k = 0; k < HS_ENTRIES; k++) {
            char *d = buf, n[12];
            uint8_t len;
            *d++ = (char)('1' + k); *d++ = '.'; *d++ = ' ';
            memcpy(d, hs_table[k].name, 3); d += 3;
            tmt_num(n, (int32_t)hs_table[k].score);
            for (len = (uint8_t)strlen(n); len < 9; len++) *d++ = ' ';
            strcpy(d, n);
            tmt_say(S_L1 + k, TMT_CENTRE, 22 + k, buf, k ? P_LGREEN : P_YELLOW, 1, 1);
        }
    }
    tmt_say(S_BIG2, TMT_CENTRE, 28, ((gs->frame / 20) & 1) ? "PRESS FIRE TO START" : "", P_YELLOW, 2, 1);
}

/* port: the initials for the high-score table (the SDK's editor) */
extern hs_editor entry_ed;
extern int32_t entry_score;
static void entry(void)
{
    char n[4];
    tmt_say(S_HP, TMT_CENTRE, 6, "GAME OVER", P_RED, 3, 1);
    tmt_say(S_BARS, TMT_CENTRE, 11, num(entry_score), P_YELLOW, 2, 1);
    tmt_say(S_GUN, TMT_CENTRE, 15, "NEW HIGH SCORE", P_WHITE, 1, 1);
    tmt_say(S_GUNLV, TMT_CENTRE, 17, "TYPE YOUR INITIALS", P_LGREEN, 1, 1);
    memcpy(n, entry_ed.name, 3);
    n[3] = 0;
    tmt_say(S_BIG1, 15, 20, n, P_YELLOW, 3, 1);
    tmt_say(S_BIG2, (uint8_t)(16 + 3 * entry_ed.pos), 24, entry_ed.pos < 3 ? "^" : "", P_WHITE, 1, 0);
}

void hud_frame(const GameState *gs, uint8_t mode)
{
    static uint8_t shown = 0xFF;
    uint8_t m = mode == MODE_TITLE ? (uint8_t)(10 + hud_title_page) : mode;
    if (m != shown) {
        tmt_clear();
        shown = m;
    }
    if (mode == MODE_TITLE) title(gs);
    else if (mode == MODE_ENTRY) entry();
    else {
        hud(gs);
        overlays(gs);
    }
}

void hud_init(void)
{
    static const uint8_t bar[8] = { 0, 0, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8 };   /* draw_hud's 5 x 6 box */
    static const uint8_t tank_icon[8] = { 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x80 };     /* its 9 x 6 lives box */
    uint8_t k, r;
    for (k = 0; k < 96; k++)
        for (r = 0; r < 7; r++) font8[k][r] = font_5x7[k][r] << 2;
    tm_init();
    ZXN_WRITE_REG(0x15, 0x13);                  /* the tilemap over the sprites over Layer 2 (U S L) */
    {
        static const uint8_t blank[8] = { 0 };
        tm_font(0, blank, 1, 0, 0);
    }
    for (k = 0; k < 96; k++) tm_glyphnx(T_FONT + k, font8[k], 1, 1, 2, 1);
    tm_glyphnx(T_FONT + 127 - 32, bar, 1, 1, 0, 0);             /* a full bar: the palette's colour */
    tm_glyphnx(T_FONT + '|' - 32, bar, 1, 3, 0, 0);             /* an empty one: pen 3 */
    tm_glyphnx(T_FONT + '~' - 32, tank_icon, 1, 1, 0, 0);
    for (k = 0; k < 7; k++) {
        tm_pal(k, 1, pal_rgb[k]);
        tm_pal(k, 2, 0);
        tm_pal(k, 3, RGB9(2, 2, 2));                            /* main.c's colour 13 */
    }
    tmt_init(slots, S_COUNT, font8[0], T_FONT, T_POOL);
}
