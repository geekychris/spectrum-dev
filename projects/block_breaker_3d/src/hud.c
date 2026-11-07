/*
 * hud.c - Block Breaker 3D's text on the Next's tilemap (the original's
 * hud(), render_title(), render_help() and the messages of render_game()):
 * 40 x 32 cells of 8 x 8 over the whole display, on top of the sprites and
 * Layer 2, colour 0 transparent so the scene shows round the letters.
 *
 *   tiles 1-95     the ROM font (32..126) with the original's one-pixel
 *                  drop shadow (g_text_shadow)
 *   tile 96        a ball (the lives), as character 127
 *   tiles 97-255   big letters (g_text_big: the font at 2, 3 or 4 times),
 *                  made when first needed, for the screen being shown
 *
 * This file is in page 84 at 0x6000 (memmap.h).
 *
 * Text is kept in slots (the score, the level, a message...): a slot is
 * redrawn only when what it says changes, so a frame normally writes
 * nothing. port: the original's rows 10 and 250 (score line, power-ups)
 * move 8 pixels in, to rows 1 and 30, inside a TV's picture.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nexttile.h"
#include "breaker.h"
#include "palfx.h"

#define T_FONT  1
#define T_BALL  96                      /* character 127 */
#define T_POOL  97

/* tilemap palettes: pen 1 the colour, pen 2 the shadow (black); render.c's
   pens 1, 31, 19, 17, 11, 3 and 27 (0xFFF 0xFD3 0x3DF 0x5E4 0xF33 0x99A
   0xFFE) in 9 bits */
enum { P_WHITE, P_YELLOW, P_CYAN, P_GREEN, P_RED, P_GREY, P_BALL };
static const uint16_t pal_rgb[7] = {
    RGB9(7, 7, 7), RGB9(7, 6, 1), RGB9(1, 6, 7), RGB9(2, 7, 2), RGB9(7, 1, 1), RGB9(4, 4, 5), RGB9(7, 7, 7)
};

static uint8_t font[96][8];             /* the ROM font, copied while the ROM is in */
static const uint8_t ball_glyph[8] = { 0x38, 0x7C, 0xFE, 0xFE, 0xFE, 0x7C, 0x38, 0x00 };

uint8_t hud_title_page;

/* ------------------------------------------------------------ big letters */

#define POOL_MAX 40
static uint8_t pool_ch[POOL_MAX], pool_sc[POOL_MAX], pool_sh[POOL_MAX], pool_tile[POOL_MAX], pool_n;
static uint16_t pool_next = T_POOL;

static uint8_t big_tile(char ch, uint8_t sc, uint8_t sh)
{
    uint8_t k, t;
    for (k = 0; k < pool_n; k++)
        if (pool_ch[k] == (uint8_t)ch && pool_sc[k] == sc && pool_sh[k] == sh) return pool_tile[k];
    if (pool_n == POOL_MAX || pool_next + sc * sc > 256) return 0;
    t = (uint8_t)pool_next;
    tm_glyphnx(t, font[(uint8_t)ch - 32], sc, 1, sh ? 2 : 0, 2);
    pool_ch[pool_n] = (uint8_t)ch;
    pool_sc[pool_n] = sc;
    pool_sh[pool_n] = sh;
    pool_tile[pool_n++] = t;
    pool_next += sc * sc;
    return t;
}

/* ------------------------------------------------------------ slots */

enum { S_SCORE, S_LEVEL, S_LIVES, S_POWER, S_AUTO, S_FLASH, S_MSG1, S_MSG2, S_BIG, S_BIG2, S_L3, S_L4, S_L5,
       S_HELP, S_COUNT = S_HELP + 14 };
typedef struct { char s[41]; uint8_t col, row, len, sc, pal, sh; } Slot;
static Slot slot[S_COUNT];
#define CENTRE 0xFF

static void draw(const Slot *p, uint8_t erase)
{
    uint8_t k, col = p->col, w = p->sc ? p->sc : 1;
    if (erase) {
        tm_fill(col, p->row, (uint8_t)(p->len * w), w, 0, 0);
        return;
    }
    if (p->sc <= 1) {
        tm_print(col, p->row, p->s, T_FONT, p->pal);
        return;
    }
    for (k = 0; k < p->len; k++, col += w) {
        uint8_t t, r, c;
        if (p->s[k] == ' ') continue;
        t = big_tile(p->s[k], p->sc, p->sh);
        if (!t) continue;
        for (r = 0; r < w; r++)
            for (c = 0; c < w; c++) tm_put(col + c, p->row + r, t + r * w + c, p->pal);
    }
}

/* slot id says s at (col, row) (col CENTRE: centred) in palette pal, at
   scale sc (1 = the font, 2-4 big letters; sh: with a shadow) */
static void say(uint8_t id, uint8_t col, uint8_t row, const char *s, uint8_t pal, uint8_t sc, uint8_t sh)
{
    Slot *p = &slot[id];
    uint8_t len = (uint8_t)strlen(s), w = sc ? sc : 1;
    if (col == CENTRE) col = (uint8_t)((40 - len * w) / 2);
    if (p->col == col && p->row == row && p->pal == pal && p->sc == sc && p->sh == sh && !strcmp(p->s, s)) return;
    if (p->len) draw(p, 1);
    strcpy(p->s, s);
    p->col = col; p->row = row; p->len = len; p->sc = sc; p->pal = pal; p->sh = sh;
    if (len) draw(p, 0);
}

static void quiet(uint8_t id) { say(id, 0, 0, "", 0, 1, 0); }

void hud_clear(void)
{
    tm_clear();
    memset(slot, 0, sizeof(slot));
    pool_n = 0;
    pool_next = T_POOL;
}

/* ------------------------------------------------------------ numbers */

static char buf[44];

static char *num(char *d, int32_t v)
{
    char t[11];
    uint8_t n = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0) *d++ = '-';
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}

static const char *cat(const char *a, int32_t v, const char *b)
{
    char *d = buf;
    while (*a) *d++ = *a++;
    d = num(d, v);
    while (*b) *d++ = *b++;
    *d = 0;
    return buf;
}

/* ------------------------------------------------------------ screens */

static const char *const help_lines[2][12] = {
    { "THE BALL FLIES DOWN THE TUNNEL TO THE",
      "BRICKS AT THE FAR END AND BACK. MOVE THE",
      "PADDLE ANY WAY TO MEET IT: WHERE IT HITS",
      "THE PADDLE STEERS IT. THE GREEN RING ON",
      "THE WALLS SHOWS HOW FAR AWAY IT IS.",
      "",
      "SILVER BRICKS TAKE SEVERAL HITS. GREY",
      "STEEL NEVER BREAKS. PINK BRICKS DROP A",
      "CAPSULE - CATCH IT WITH THE PADDLE:",
      " W WIDE PADDLE    S SLOW BALL",
      " L EXTRA LIFE     P PLOUGH THROUGH",
      "CLEAR ALL 8 LEVELS TO WIN." },
    /* port: the Spectrum's keys */
    { "QAOP / CURSORS / JOYSTICK  MOVE",
      "SPACE / FIRE               SERVE",
      "ENTER                      PAUSE",
      "B                          AUTOPILOT",
      "H                          THESE PAGES",
      "BREAK (CAPS SPACE)         TITLE",
      "",
      "BRICKS 10, TOUGH 30, BONUS 50,",
      "+5 FOR EACH LAYER DEEPER.",
      "CAPSULE 100. LEVEL CLEAR 250 X LEVEL.",
      "",
      "THE BALL SPEEDS UP AS YOU RALLY." },
};

static void help(uint8_t page)
{
    uint8_t k;
    say(S_BIG, CENTRE, 2, page == 1 ? "HOW TO PLAY" : "CONTROLS", P_CYAN, 2, 0);
    for (k = 0; k < 12; k++) say(S_HELP + k, 0, 6 + 2 * k, help_lines[page - 1][k], P_WHITE, 1, 0);   /* lines up to 40 */
    say(S_HELP + 12, 1, 30, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", P_GREY, 1, 0);
    say(S_HELP + 13, 19, 30, "H: NEXT  FIRE: PLAY", P_YELLOW, 1, 0);
}

static void title(const BBGame *g)
{
    (void)g;
    if (hud_title_page == 0) {
        say(S_BIG, 3, 3, "BLOCK BREAKER", P_YELLOW, 2, 1);
        say(S_BIG2, 16, 7, "3D", P_CYAN, 4, 1);
        quiet(S_L3); quiet(S_L4); quiet(S_L5); quiet(S_MSG1); quiet(S_MSG2);
    } else {
        uint8_t k;
        say(S_BIG, CENTRE, 3, "HIGH SCORES", P_YELLOW, 2, 1);
        quiet(S_BIG2);
        /* the table: five lines in slots S_L3.. S_L5, S_MSG1, S_MSG2 */
        for (k = 0; k < HS_ENTRIES; k++) {
            static const uint8_t id[HS_ENTRIES] = { S_L3, S_L4, S_L5, S_MSG1, S_MSG2 };
            char *d = buf;
            *d++ = (char)('1' + k); *d++ = '.'; *d++ = ' ';
            memcpy(d, hs_table[k].name, 3); d += 3;
            *d++ = ' '; *d++ = ' ';
            {
                char n[12];
                uint8_t len, pad;
                num(n, (int32_t)hs_table[k].score);
                len = (uint8_t)strlen(n);
                for (pad = len; pad < 7; pad++) *d++ = ' ';
                strcpy(d, n);
            }
            say(id[k], CENTRE, 8 + 2 * k, buf, k ? P_WHITE : P_YELLOW, 1, 0);
        }
    }
    say(S_SCORE, CENTRE, 23, cat("START AT LEVEL ", br.start_level, "  (UP/DOWN)"), P_GREEN, 1, 0);
    say(S_LEVEL, CENTRE, 25, "PRESS FIRE TO PLAY", (frame_count / 24) & 1 ? P_YELLOW : P_WHITE, 1, 0);
    say(S_LIVES, CENTRE, 27, cat("BEST ", br.best, "     H: HOW TO PLAY"), P_GREY, 1, 0);
}

static void play(const BBGame *g)
{
    uint8_t k;
    char *d;
    say(S_SCORE, 1, 1, cat("SCORE ", g->score, ""), P_WHITE, 1, 0);
    say(S_LEVEL, 16, 1, cat("LEVEL ", g->level, ""), P_YELLOW, 1, 0);
    /* the lives: balls from x 210 (render.c), one every two cells */
    for (k = 0, d = buf; k < g->lives && k < 8; k++) { *d++ = (char)(T_BALL - T_FONT + 32); *d++ = ' '; }
    *d = 0;
    say(S_LIVES, 26, 1, buf, P_BALL, 1, 0);
    d = buf;
    *d = 0;
    if (g->wide_t) { strcpy(d, "WIDE "); d += 5; }
    if (g->slow_t) { strcpy(d, "SLOW "); d += 5; }
    if (g->plough_t) strcpy(d, "PLOUGH");
    say(S_POWER, 1, 30, buf, g->plough_t && !g->wide_t && !g->slow_t ? P_RED : g->slow_t && !g->wide_t ? P_CYAN : P_YELLOW, 1, 0);
    say(S_AUTO, 31, 30, br.autoplay ? "AUTO" : "", P_GREEN, 1, 0);
    if (g->stuck && !g->between && br.state == BS_PLAY) {
        char *e = buf;
        const char *nm = bb_level_names[g->level - 1];
        e = num(strcpy(e, "LEVEL ") + 6, g->level);
        *e++ = ':'; *e++ = ' ';
        strcpy(e, nm);
        say(S_MSG1, CENTRE, 11, buf, P_YELLOW, 1, 0);
        say(S_MSG2, CENTRE, 21, "FIRE TO SERVE", P_WHITE, 1, 0);
    } else {
        quiet(S_MSG1);
        quiet(S_MSG2);
    }
    if (br.state == BS_OVER) {
        say(S_BIG, g->won ? 12 : 11, 12, g->won ? "YOU WIN" : "GAME OVER", g->won ? P_GREEN : P_RED, 2, 0);
        say(S_L3, CENTRE, 17, cat("SCORE ", g->score, ""), P_WHITE, 1, 0);
        say(S_L4, CENTRE, 19, br.state_timer == 0 ? "PRESS FIRE" : "", P_YELLOW, 1, 0);
    } else if (g->between) {
        say(S_BIG, 9, 12, "CLEARED!", P_GREEN, 3, 0);
        quiet(S_L3); quiet(S_L4);
    } else {
        quiet(S_BIG); quiet(S_L3); quiet(S_L4);
    }
    say(S_L5, CENTRE, 16, br.paused == 1 ? "PAUSED" : "", P_WHITE, 1, 0);
    say(S_FLASH, CENTRE, 4, br.flash_timer > 0 ? br.flash : "", P_YELLOW, 1, 0);
}

/* the high-score entry (port: new; the SDK's initials editor) */
extern hs_editor entry_ed;
static void entry(const BBGame *g)
{
    char n[4];
    say(S_BIG, CENTRE, 12, "NEW HIGH SCORE", P_YELLOW, 1, 0);
    say(S_L3, CENTRE, 14, cat("SCORE ", g->score, ""), P_WHITE, 1, 0);
    say(S_L4, CENTRE, 16, "TYPE YOUR INITIALS", P_GREY, 1, 0);
    memcpy(n, entry_ed.name, 3);
    n[3] = 0;
    say(S_L5, 17, 18, n, P_CYAN, 2, 0);
    say(S_MSG2, 17 + 2 * entry_ed.pos, 20, entry_ed.pos < 3 ? "^" : " ", P_YELLOW, 1, 0);
}

void hud_frame(const BBGame *g)
{
    static uint8_t mode = 0xFF;
    uint8_t m = br.help ? 3 + br.help : br.state == BS_TITLE ? (uint8_t)(10 + hud_title_page) : br.state == BS_ENTRY ? 2 : 1;
    if (m != mode) {                    /* another screen: start from nothing */
        hud_clear();
        mode = m;
    }
    if (br.help) help(br.help);
    else if (br.state == BS_TITLE) title(g);
    else if (br.state == BS_ENTRY) entry(g);
    else play(g);
}

void hud_init(void)
{
    uint8_t k;
    memcpy(font, (const void *)0x3D00, sizeof(font));   /* the ROM font (main() calls this while the ROM is in) */
    tm_init();
    ZXN_WRITE_REG(0x15, 0x13);                          /* tilemap over sprites over Layer 2 (U S L), sprites over the border */
    {
        static const uint8_t blank[8] = { 0 };
        tm_font(0, blank, 1, 0, 0);                     /* tile 0 blank */
    }
    for (k = 0; k < 95; k++) tm_glyphnx(T_FONT + k, font[k], 1, 1, 2, 1);
    tm_glyphnx(T_BALL, ball_glyph, 1, 1, 0, 0);
    for (k = 0; k < 7; k++) {
        tm_pal(k, 1, pal_rgb[k]);
        tm_pal(k, 2, 0);
    }
    hud_clear();
}
