/*
 * hud.c - the original's HUD (main_os4.c hud(), hud_player(), hud_title(),
 * banner()) on the Next's tilemap, over the course picture and the sprites:
 * its texts in its pens and places, the big clock and the banners in a
 * double-size font with a drop shadow, the progress bar.
 *
 * port: the original darkens the strip behind the HUD and the banners'
 * boxes (render_shade: the 3DO's pixel processor). Here the copper shows
 * Layer 2's second palette - the course's colours at half brightness
 * (5/8 behind the title) - on those raster lines.
 *
 * The original's screen is 320 x 240; this one 320 x 256, so its y + 8 is
 * the place here (rows of 8 pixels). Text is redrawn only when it changes.
 * In bank 17: banks.c's stubs hud_init and hud_frame map it.
 */
#include <string.h>
#include <stdio.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "nextcopper.h"
#include "palfx.h"
#include "game.h"
#include "hud.h"


#define T_FONT   1                       /* 1..64: characters 32..95, shadowed */
#define T_BAR    65                      /* 65..73: a bar cell with 0..8 pixels filled */
#define T_BIGD   75                      /* 75..118: the big digits 0-9 and '.', 4 tiles each */
#define T_BIGL   119                     /* 119..174: big letters, defined as needed */
#define BIGL_MAX 14

/* the original's pens (pen_rgb) in RGB333: palette offset = pen */
enum { P_WHITE = 1, P_AMBER, P_CYAN, P_RED, P_GREEN, P_DIM, P_GOLD, P_SILVER, P_BRONZE, P_SHADOW,
       P_ORANGE, P_BAR, P_SEL, P_FAINT, P_COUNT };
static const uint16_t pen_rgb[P_COUNT] = {
    0, RGB9(7, 7, 7), RGB9(7, 6, 2), RGB9(4, 6, 7), RGB9(7, 3, 2), RGB9(4, 7, 4), RGB9(5, 5, 6), RGB9(7, 6, 2),
    RGB9(6, 7, 7), RGB9(6, 4, 2), RGB9(0, 0, 0), RGB9(7, 5, 3), RGB9(2, 2, 3), RGB9(2, 3, 4), RGB9(3, 3, 4)
};

/* ---- tiles ---- */

static void shadow_tile(uint8_t tile, const uint8_t *g)
{
    uint8_t def[32], y, x, *d = def;
    for (y = 0; y < 8; y++) {
        uint8_t row = g[y], sh = y ? g[y - 1] >> 1 : 0;
        for (x = 0; x < 8; x += 2, d++) {
            uint8_t m0 = 0x80 >> x, m1 = 0x40 >> x;
            uint8_t a = row & m0 ? 1 : sh & m0 ? 2 : 0, b = row & m1 ? 1 : sh & m1 ? 2 : 0;
            *d = a << 4 | b;
        }
    }
    tm_def(tile, def, 1);
}

static void bar_tiles(void)
{
    uint8_t def[32], n, y, x;
    for (n = 0; n <= 8; n++) {
        memset(def, 0, 32);
        for (y = 0; y < 2; y++)
            for (x = 0; x < 8; x++) {
                uint8_t pen = x < n ? 1 : 3;     /* filled: the ink; else the bar's pen */
                def[y * 4 + x / 2] |= (x & 1) ? pen : pen << 4;
            }
        tm_def(T_BAR + n, def, 1);
    }
}

/* the big letters on show: char -> its 4 tiles */
static char bigl_chr[BIGL_MAX];
static uint8_t bigl_n;

static uint8_t big_tile(char c)
{
    uint8_t i;
    if (c >= '0' && c <= '9') return T_BIGD + (uint8_t)(c - '0') * 4;
    if (c == '.') return T_BIGD + 40;
    for (i = 0; i < bigl_n; i++)
        if (bigl_chr[i] == c) return T_BIGL + i * 4;
    if (bigl_n >= BIGL_MAX) bigl_n = 0;      /* out of room: start again (texts are short) */
    bigl_chr[bigl_n] = c;
    tm_glyph2x(T_BIGL + bigl_n * 4, rom_font + (c - 32) * 8, 1, 2);
    return T_BIGL + 4 * bigl_n++;
}

void hud_init_b(void)
{
    uint8_t k;
    uint8_t blank[32];
    tm_init();
    memset(blank, 0, 32);
    tm_def(0, blank, 1);                     /* tile 0: nothing (bank 5 held the ULA screen) */
    for (k = 0; k < 64; k++) shadow_tile(T_FONT + k, rom_font + k * 8);
    bar_tiles();
    for (k = 0; k < 10; k++) tm_glyph2x(T_BIGD + k * 4, rom_font + (16 + k) * 8, 1, 2);
    tm_glyph2x(T_BIGD + 40, rom_font + 14 * 8, 1, 2);
    for (k = 1; k < P_COUNT; k++) {
        tm_pal(k, 1, pen_rgb[k]);
        tm_pal(k, 2, RGB9(0, 0, 0));
        tm_pal(k, 3, pen_rgb[P_BAR]);
    }
}

/* ---- what is on the tilemap: a character and a pen per cell ---- */

#define ROWS 32
#define BIG  0x40                        /* pen flag: a big character's top-left cell */
#define PART 0x80                        /* pen flag: the rest of a big character */
static char want[ROWS][40], shown[ROWS][40];
static uint8_t wpen[ROWS][40], spen[ROWS][40];

static void put_text(uint8_t row, uint8_t col, const char *s, uint8_t pen, uint8_t big)
{
    for (; *s && col < 40 && row < ROWS; s++) {
        if (big) {
            if (col + 1 >= 40 || row + 1 >= ROWS) break;
            want[row][col] = *s;
            wpen[row][col] = pen | BIG;
            want[row][col + 1] = want[row + 1][col] = want[row + 1][col + 1] = ' ';
            wpen[row][col + 1] = wpen[row + 1][col] = wpen[row + 1][col + 1] = PART;
            col += 2;
        } else {
            want[row][col] = *s;
            wpen[row][col] = pen;
            col++;
        }
    }
}

static void text_at(uint8_t row, uint8_t col, const char *s, uint8_t pen) { put_text(row, col, s, pen, 0); }
static void ctext(uint8_t row, const char *s, uint8_t pen)
{
    put_text(row, 20 - (uint8_t)(strlen(s) + 1) / 2, s, pen, 0);
}
static void big_c(uint8_t row, const char *s, uint8_t pen) { put_text(row, 20 - (uint8_t)strlen(s), s, pen, 1); }
static void text_also(uint8_t row, uint8_t col, const char *s, uint8_t pen) { put_text(row, col, s, pen, 0); }

static char bigs_prev[48];

/* the frame's HUD is in want[]: draw the rows that changed */
static void flush(void)
{
    uint8_t r, c, all = 0, n = 0;
    char bigs[48];
    /* the big characters on show: when they change, the letter tiles start afresh */
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < 40; c++)
            if ((wpen[r][c] & BIG) && n < sizeof(bigs) - 1 && want[r][c] > '9') bigs[n++] = want[r][c];   /* letters: digits have fixed tiles */
    bigs[n] = 0;
    if (strcmp(bigs, bigs_prev)) {
        strcpy(bigs_prev, bigs);
        bigl_n = 0;
        all = 1;
    }
    for (r = 0; r < ROWS; r++) {
        uint8_t again = 0;               /* the letter tiles were redefined: rows with big letters again */
        if (all)
            for (c = 0; c < 40; c++)
                if (wpen[r][c] & BIG) { again = 1; break; }
        if (!again && !memcmp(want[r], shown[r], 40) && !memcmp(wpen[r], spen[r], 40)) continue;
        for (c = 0; c < 40; c++) {
            uint8_t pen = wpen[r][c];
            if (pen & PART) continue;            /* drawn with its big character */
            if (pen & BIG) {
                uint8_t t = big_tile(want[r][c]), k = pen & 15;
                tm_put(c, r, t, k);
                tm_put(c + 1, r, t + 1, k);
                tm_put(c, r + 1, t + 2, k);
                tm_put(c + 1, r + 1, t + 3, k);
            } else if (want[r][c] != shown[r][c] || pen != spen[r][c] || (spen[r][c] & (BIG | PART)))
                tm_put(c, r, (uint8_t)want[r][c] >= 0x80 ? T_BAR + (uint8_t)want[r][c] - 0x80 :
                       want[r][c] > ' ' && want[r][c] < 96 ? T_FONT + want[r][c] - 32 : 0, pen);
        }
        memcpy(shown[r], want[r], 40);
        memcpy(spen[r], wpen[r], 40);
    }
}

static void clear_want(void)
{
    memset(want, ' ', sizeof(want));
    memset(wpen, 0, sizeof(wpen));
}

/* ---- the shade: copper ranges of rows on Layer 2's second palette ---- */

static uint8_t shade_key;

/* rows [y0, y1) of the 320 x 256 screen shaded to k/8, up to 3 ranges */
static uint8_t sh_y0[3], sh_y1[3], sh_n, sh_k;

static void shade(uint8_t y0, uint8_t y1)
{
    if (sh_n < 3) { sh_y0[sh_n] = y0; sh_y1[sh_n] = y1; sh_n++; }
}

static void shade_apply(void)
{
    uint8_t key = sh_n | sh_k << 2, i;
    for (i = 0; i < sh_n; i++) key ^= sh_y0[i] * 3 + sh_y1[i] * 7 + i * 31;
    hud_dark = sh_k;                     /* main.c loads it in the vertical blank (the copper writes 0x43 too) */
    if (key == shade_key) return;
    shade_key = key;
    copper_begin();
    /* the list runs from line 248: ranges are in screen rows, top first; the
       320 x 256 rows 0..31 are lines 280..311 (before 248's wrap: order) */
    for (i = 0; i < sh_n; i++) {
        copper_wait(L2Y_TO_LINE(sh_y0[i]), 0);
        copper_move(0x43, 0x14);             /* show Layer 2's second palette (edit: the first) */
        copper_wait(L2Y_TO_LINE(sh_y1[i]), 0);
        copper_move(0x43, 0x10);
    }
    copper_end();
}

/* ---- the original's HUD ---- */

static void fmt_time(char *buf, long steps)       /* Progress.Format: mm:ss.s */
{
    long t = steps * 2;                            /* hundredths */
    if (steps <= 0) { strcpy(buf, "--:--"); return; }
    if (t >= 6000) sprintf(buf, "%ld:%02ld.%ld", t / 6000, (t % 6000) / 100, (t % 100) / 10);
    else sprintf(buf, "%ld.%ld", t / 100, (t % 100) / 10);
}

/* game.c's medal_for (in the other bank) */
static int medal_of(long t)
{
    long ms = t * 20;
    if (t <= 0) return MEDAL_NONE;
    if (C.gold_ms > 0 && ms <= C.gold_ms) return MEDAL_GOLD;
    if (C.silver_ms > 0 && ms <= C.silver_ms) return MEDAL_SILVER;
    if (C.bronze_ms > 0 && ms <= C.bronze_ms) return MEDAL_BRONZE;
    return MEDAL_NONE;
}

static const char *medal_name(int m) { return m == MEDAL_GOLD ? "GOLD" : m == MEDAL_SILVER ? "SILVER" : m == MEDAL_BRONZE ? "BRONZE" : ""; }
static uint8_t medal_pen(int m) { return m == MEDAL_GOLD ? P_GOLD : m == MEDAL_SILVER ? P_SILVER : m == MEDAL_BRONZE ? P_BRONZE : P_FAINT; }
static uint8_t blink(void) { return (G.clock / 18) % 3 != 2; }

static void banner(const char *title, uint8_t pen, const char *sub)
{
    shade(100, 155);                     /* render_shade(92 .. 146) */
    big_c(13, title, pen);
    if (sub && sub[0]) ctext(17, sub, P_WHITE);
}

/* the progress bar: cells 10..29 of row 4, 0..160 pixels filled (the cells
   are bar tiles: characters 0x80 + pixels in the cell) */
static void progress_bar(uint8_t px)
{
    uint8_t c, n;
    for (c = 0; c < 20; c++) {
        n = px >= (uint8_t)(c * 8 + 8) ? 8 : px > c * 8 ? px - c * 8 : 0;
        want[4][10 + c] = (char)(0x80 + n);
        wpen[4][10 + c] = P_CYAN;
    }
}

/* what the HUD shows: it is redrawn only when this changes */
typedef struct {
    uint8_t state, level, sel, paused, demo, blink, page, music, entry, ipos, dying, fin, out, pr;
    long tl, ct;
    int deaths;
    uint32_t score;
    char ini[4];
} Shown;
static Shown last_key;
static long progress;                    /* course_progress, every 8th frame (it walks the route) */
static uint8_t progress_age;

static void hud_player(void)
{
    Player *p = &G.p[0];
    char buf[48], t[16];
    shade(0, 29);                         /* render_shade(0 .. 20) */
    sprintf(buf, "%d/6 %s", G.level + 1, C.name);   /* (course_count is in bank 14) */
    text_at(1, 1, buf, P_CYAN);
    sprintf(t, "%ld.%ld", p->time_left / 50, (p->time_left % 50) / 5);
    put_text(1, 25, t, p->time_left <= 500 ? P_RED : P_AMBER, 1);
    sprintf(buf, "FALLS %d", p->deaths);
    text_at(3, 39 - (uint8_t)strlen(buf), buf, P_AMBER);
    progress_bar(last_key.pr);
    fmt_time(t, p->course_time);
    if (G.best[G.level] > 0) {
        char b[16];
        fmt_time(b, G.best[G.level]);
        sprintf(buf, "%s   BEST %s", t, b);
    } else strcpy(buf, t);
    ctext(5, buf, P_DIM);
    if (!G.demo) {
        sprintf(buf, "%lu", hud_score);   /* port: the score (the high-score table's) */
        text_also(5, 1, buf, P_DIM);
    }
    if (p->dying) banner(p->death_reason, P_RED, "-3 SECONDS");
    else if (p->out_of_time && G.state == ST_PLAYING) banner("OUT OF TIME", P_RED, "");
    else if (p->finished && G.state == ST_PLAYING) { fmt_time(t, p->finish_time); banner("FINISHED", P_GREEN, t); }
}

static const char *const names[6] = { "WARM-UP", "FIRST DROP", "SWITCHBACK", "SKYWAY", "TOPSY-TURVY", "THE GAUNTLET" };

static void hud_title(void)
{
    char buf[48], t[16];
    uint8_t i;
    sh_k = 5;                             /* render_shade(0, 0, 320, 240, 5) */
    shade(0, 255);
    big_c(2, "ROLLING STEEL", P_AMBER);
    ctext(5, "SIX COURSES, ONE CLOCK", P_CYAN);
    if (((G.clock / 300) & 1) && hs_table[0].score) {
        /* port: the high-score table, in turn with the courses */
        ctext(8, "HIGH SCORES", P_AMBER);
        for (i = 0; i < HS_ENTRIES; i++) {
            sprintf(buf, "%d. %s %7lu", i + 1, hs_table[i].name, hs_table[i].score);
            ctext(10 + i, buf, i ? P_DIM : P_WHITE);
        }
    } else
        for (i = 0; i < 6; i++) {
            uint8_t sel = i == G.title_select;
            fmt_time(t, G.best[i]);
            sprintf(buf, "%c %d  %-12s %7s %s", sel ? '>' : ' ', i + 1, names[i], t,
                    G.best[i] > 0 && i == G.level ? medal_name(medal_of(G.best[i])) : "");
            text_at(8 + i, 5, buf, sel ? P_WHITE : G.best[i] > 0 ? P_AMBER : P_DIM);
        }
    ctext(15, "ONE PLAYER", P_CYAN);
    if (G.best_run > 0) {
        fmt_time(t, G.best_run);
        sprintf(buf, "BEST FULL RUN %s (%ld FALLS)", t, G.best_run_falls);
        ctext(16, buf, P_CYAN);
    }
    ctext(18, "UP/DOWN COURSE  SPACE/FIRE START", P_DIM);
    ctext(19, "QAOP, CURSOR, JOYSTICK: PUSH", P_FAINT);
    ctext(20, G.music_on ? "S MUSIC: ON   ENTER PAUSE" : "S MUSIC: OFF   ENTER PAUSE", P_FAINT);
    if (blink()) big_c(25, "PRESS SPACE", P_GREEN);
}

void hud_frame_b(void)
{
    char buf[48], t[16];
    Shown k;
    const Player *pl = &G.p[0];
    if (G.state != ST_TITLE && (progress_age++ & 7) == 0) progress = course_progress(pl);
    memset(&k, 0, sizeof k);
    k.state = (uint8_t)G.state; k.level = (uint8_t)G.level; k.sel = (uint8_t)G.title_select;
    k.paused = (uint8_t)G.paused; k.demo = (uint8_t)G.demo; k.blink = blink();
    k.page = (uint8_t)((G.clock / 300) & 1); k.music = (uint8_t)G.music_on;
    k.entry = hud_entry; k.ipos = hud_initials_pos; memcpy(k.ini, hud_initials, 4);
    k.dying = (uint8_t)pl->dying; k.fin = (uint8_t)pl->finished; k.out = (uint8_t)pl->out_of_time;
    k.pr = (uint8_t)((160L * progress) >> 16);
    k.tl = pl->time_left / 5; k.ct = pl->course_time / 5; k.deaths = pl->deaths; k.score = hud_score;
    if (!memcmp(&k, &last_key, sizeof k)) return;
    memcpy(&last_key, &k, sizeof k);
    clear_want();
    sh_n = 0;
    sh_k = 4;
    if (G.state == ST_TITLE) {
        hud_title();
    } else {
        hud_player();
        if (hud_entry) {
            uint8_t k;
            banner("NEW HIGH SCORE", P_AMBER, "");
            for (k = 0; k < 3; k++) {
                buf[k * 2] = hud_initials[k];
                buf[k * 2 + 1] = ' ';
            }
            buf[5] = 0;
            ctext(17, buf, P_WHITE);
            buf[0] = 0;
            for (k = 0; k < 3; k++) strcat(buf, k == hud_initials_pos ? "- " : "  ");
            ctext(18, buf, P_AMBER);
        } else if (G.state == ST_CLEAR) {
            fmt_time(t, G.last_course_time);
            sprintf(buf, "%s %s%s", t, medal_name(G.last_medal), G.last_was_best ? " - NEW BEST" : "");
            banner("COURSE CLEAR", G.last_medal ? medal_pen(G.last_medal) : P_GREEN, buf);
        } else if (G.state == ST_WON) {
            fmt_time(t, G.p[0].run_time);
            sprintf(buf, "%s FALLS %d%s", t, G.p[0].deaths, G.last_was_best ? " - NEW BEST RUN" : "");
            banner("ALL CLEAR", P_GREEN, buf);
        } else if (G.state == ST_GAMEOVER)
            banner("OUT OF TIME", P_RED, "PRESS SPACE TO TRY AGAIN");
        if (G.paused) banner("PAUSED", P_WHITE, "ENTER TO CARRY ON");
        if (G.demo && blink()) ctext(28, "DEMO - PRESS SPACE", P_DIM);
    }
    flush();
    shade_apply();
}
