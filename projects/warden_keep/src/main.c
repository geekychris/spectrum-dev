/*
 * Warden Keep for the Spectrum Next (the original's main.cpp and
 * render.cpp): start-up, the 50 Hz loop with the original's auto-repeat,
 * the picture and the screens.
 *
 * The room (floor, door, back walls, the HUD bar) is one 320 x 256 Layer 2
 * picture made on the host from the original's drawing (tools/gen_gfx.py)
 * and loaded with the .nex: it never changes. The barrels, the jewels and
 * the player are sprite groups, numbered in the original's painter's
 * order (render_frame: the objects by wx + wy, the player before the
 * first object further forward), so they hide each other as the original
 * draws them; nothing can stand behind the back walls, so no masks. Text
 * is the tilemap, over everything: the HUD, the title, the end screens.
 *
 * port: keys. Q A O P, the cursor keys, Sinclair and Kempston move (up is
 * the original's cursor up: north, up-right on the screen); SPACE, ENTER
 * or fire start; BREAK goes back to the title (the original's Esc quits).
 * port: the high-score table and its initials, and the title showing it.
 * port: R on the title switches between the timed game (the port's clock
 * and bonus) and the relaxed one (the original: no clock; such games
 * don't go in the table, which is for timed ones).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <input.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nextspr.h"
#include "nexttile.h"
#include "wk.h"
#include "gfx_data.h"
#include "sound.h"

#define HOLD_DELAY   12                  /* the original's auto-repeat: first after 12 frames, */
#define REPEAT_EVERY 6                   /* then every 6 */
#define T_FONT       1                   /* tiles 1..96: characters 32..127 */
#define T_BIG        100                 /* 2x glyphs, 4 tiles each */
#define T_SOLID      240                 /* pen 3 everywhere: bars */
#define BIG_CHARS    "WARDENKPTIMUOVEYJLC"
#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))

enum { PEN_WHITE, PEN_GOLD, PEN_PALE, PEN_RED, PEN_GREEN, PEN_BLACK, PEN_ON_RED };    /* tilemap palette offsets */

static GameState g;
static hs_editor ed;
static int8_t ed_place = -1;
static uint8_t entering, shown_mode = 0xFF, title_page, mode_key;
static uint16_t title_timer;
static char tmp[40];

/* ---- text (the tilemap) ---- */
static uint8_t big_tile(char c)
{
    const char *p = strchr(BIG_CHARS, c);
    return p ? T_BIG + (uint8_t)(p - BIG_CHARS) * 4 : 0;
}

/* a ROM glyph at twice its size as 4 tiles (TL TR BL BR): pen 1 the glyph,
   pen 2 its shadow a pixel down-right, pen 3 behind (the bar's colour: a
   transparent background would show black through the bar) */
static void big_glyph(uint8_t tile, const uint8_t *gl)
{
    uint8_t q, y, x, def[32];
    for (q = 0; q < 4; q++) {
        uint8_t *d = def;
        for (y = 0; y < 8; y++) {
            uint8_t sy = (uint8_t)((q >> 1) * 8 + y), row = gl[sy >> 1];
            uint8_t up = sy >= 2 ? gl[(sy - 2) >> 1] : 0;
            for (x = 0; x < 8; x += 2, d++) {
                uint8_t sx0 = (uint8_t)((q & 1) * 8 + x), sx1 = sx0 + 1, a, b;
                a = row & (0x80 >> (sx0 >> 1)) ? 1 : sx0 >= 2 && (up & (0x80 >> ((sx0 - 2) >> 1))) ? 2 : 3;
                b = row & (0x80 >> (sx1 >> 1)) ? 1 : sx1 >= 2 && (up & (0x80 >> ((sx1 - 2) >> 1))) ? 2 : 3;
                *d = (uint8_t)(a << 4 | b);
            }
        }
        tm_def(tile + q, def, 1);
    }
}

static void text_init(void)
{
    static const uint8_t blank[8] = { 0 };
    static const uint16_t ink[7] = { RGB9(7, 7, 7), RGB9(7, 6, 1), RGB9(7, 7, 3), RGB9(7, 2, 1), RGB9(3, 7, 3),
                                     RGB9(0, 0, 0), RGB9(0, 0, 0) };
    uint8_t k, def[32];
    const char *c;
    tm_init();
    tm_font(0, blank, 1, 1, 0);
    tm_font(T_FONT, (const uint8_t *)0x3D00, 96, 1, 0);          /* the ROM's characters */
    for (c = BIG_CHARS; *c; c++) big_glyph(big_tile(*c), (const uint8_t *)0x3D00 + (*c - 32) * 8);
    memset(def, 0x33, sizeof(def));
    tm_def(T_SOLID, def, 1);
    for (k = 0; k < 7; k++) {
        uint8_t red = k == PEN_RED || k == PEN_ON_RED;
        tm_pal(k, 1, ink[k]);
        tm_pal(k, 2, red ? RGB9(3, 0, 0) : RGB9(4, 3, 0));          /* the 2x glyphs' shadow */
        tm_pal(k, 3, red ? RGB9(5, 1, 0) : RGB9(6, 5, 1));          /* bars: the original's pen 2 (gold); red: time up */
    }
}

static void text(uint8_t col, uint8_t row, const char *s, uint8_t pen) { tm_print(col, row, s, T_FONT, pen); }
static void ctext(uint8_t row, const char *s, uint8_t pen) { text(20 - (uint8_t)(strlen(s) + 1) / 2, row, s, pen); }

static void big(uint8_t col, uint8_t row, const char *s, uint8_t pen)
{
    for (; *s; s++, col += 2) {
        uint8_t t = big_tile(*s);
        if (!t) continue;                /* a space: the bar shows */
        tm_put(col, row, t, pen);
        tm_put(col + 1, row, t + 1, pen);
        tm_put(col, row + 1, t + 2, pen);
        tm_put(col + 1, row + 1, t + 3, pen);
    }
}

static void bar(uint8_t row, uint8_t h, uint8_t pen) { tm_fill(0, row, TM_COLS, h, T_SOLID, pen); }

/* ---- the picture ---- */
static void room_visible(uint8_t on)
{
    z80_outp(0x123B, on ? 0x02 : 0x00);
    if (!on) sprg_hide_from(0);
}

/* the original's iso_project: the top's middle of a thing at height wz */
static int16_t iso_x(int8_t wx, int8_t wy) { return (int16_t)(wx - wy) * 16 + 160; }
static int16_t iso_y(int8_t wx, int8_t wy, int8_t wz) { return (int16_t)(wx + wy) * 8 - wz * 16 + 96; }

static uint8_t show_player(uint8_t n)
{
    Player *p = &g.player;
    return sprg_show(n, iso_x(p->wx, p->wy) - 16, iso_y(p->wx, p->wy, 2) - 8, 2, 3, PAT_PLAYER0 + p->facing * 6, 0);
}

/* render_frame's painter's order, as sprite numbers (a higher one is drawn over) */
static void draw_room(void)
{
    uint8_t idx[MAX_OBJECTS], n = 0, i, j, s = 0, drew = 0;
    int8_t pk = g.player.wx + g.player.wy;
    for (i = 0; i < g.num_objects; i++)
        if (g.objects[i].kind == O_BARREL || g.objects[i].kind == O_JEWEL) idx[n++] = i;   /* walls: Layer 2 */
    for (i = 1; i < n; i++) {                                     /* sort_objects: insertion, by wx + wy */
        uint8_t cur = idx[i], ck = g.objects[cur].wx + g.objects[cur].wy;
        for (j = i; j > 0 && g.objects[idx[j - 1]].wx + g.objects[idx[j - 1]].wy > ck; j--) idx[j] = idx[j - 1];
        idx[j] = cur;
    }
    for (i = 0; i < n; i++) {
        Prop *o = &g.objects[idx[i]];
        if (!drew && pk < (int8_t)(o->wx + o->wy)) {
            s = show_player(s);
            drew = 1;
        }
        if (o->kind == O_BARREL)
            s = sprg_show(s, iso_x(o->wx, o->wy) - 16, iso_y(o->wx, o->wy, 1) - 8, 2, 2, PAT_BARREL, 0);
        else
            s = sprg_show(s, iso_x(o->wx, o->wy) - 8, iso_y(o->wx, o->wy, 0) - 13, 1, 1, PAT_JEWEL, 0);
    }
    if (!drew) s = show_player(s);
    sprg_hide_from(s);
}

/* ---- the HUD (render.cpp draw_hud: SCORE and JEWELS on the bar; port: the clock) ---- */
static int16_t hud_score = -1, hud_secs = -1;
static int8_t hud_jewels = -1;

static void hud(void)
{
    int16_t secs = g.relaxed ? 0 : (g.time_left + 49) / 50;
    if (g.score != hud_score) {
        hud_score = g.score;
        sprintf(tmp, "SCORE %06d", g.score);
        text(1, 30, tmp, PEN_WHITE);
    }
    if ((int8_t)g.jewels_left != hud_jewels) {
        hud_jewels = (int8_t)g.jewels_left;
        sprintf(tmp, "JEWELS %d", g.jewels_left);
        text(29, 30, tmp, PEN_WHITE);
    }
    if (secs != hud_secs) {
        hud_secs = secs;
        if (g.relaxed) text(15, 30, "RELAXED", PEN_GOLD);
        else {
            sprintf(tmp, "TIME %2d", secs);
            text(16, 30, tmp, secs <= 10 ? PEN_RED : PEN_GOLD);
        }
    }
}

/* ---- the screens ---- */
static void hiscores(uint8_t row)
{
    uint8_t k;
    ctext(row, "BEST WARDENS", PEN_GOLD);
    for (k = 0; k < HS_ENTRIES; k++) {
        sprintf(tmp, "%u  %s  %06lu", k + 1, hs_table[k].name, hs_table[k].score);
        ctext(row + 2 + k, tmp, k == 0 ? PEN_GREEN : PEN_WHITE);
    }
}

/* render_title: a gold bar with the name, the lines under it */
static void draw_title(void)
{
    tm_clear();
    bar(5, 5, PEN_GOLD);
    big(9, 6, "WARDEN  KEEP", PEN_BLACK);
    if (title_page) {
        hiscores(13);
    } else {
        ctext(14, "AN ISOMETRIC KEEP TO PLUNDER", PEN_PALE);
        ctext(16, "PUSH BARRELS, GRAB JEWELS", PEN_WHITE);
        text(6, 20, "QAOP/CURSOR/JOY : MOVE", PEN_PALE);
        text(6, 21, "SPACE           : BEGIN", PEN_PALE);
        text(6, 22, "BREAK           : TITLE", PEN_PALE);
        text(6, 23, "R               : MODE", PEN_PALE);
        ctext(25, g.relaxed ? "RELAXED: NO CLOCK (THE ORIGINAL)" : "  TIMED: 60 SECONDS, A BONUS   ", PEN_GOLD);
        sprintf(tmp, "BEST %06lu", hs_table[0].score);
        ctext(26, tmp, PEN_GOLD);
    }
}

static void title_blink(void)
{
    ctext(28, (g.tick / 15) & 1 ? "PRESS  SPACE  TO  BEGIN" : "                       ", PEN_RED);
}

/* render_endscreen: a bar with the message, the score; port: TIME UP, the bonus */
static void draw_end(void)
{
    tm_clear();
    if (g.mode == GM_WIN) {
        bar(10, 4, PEN_GOLD);
        big(1, 11, "EVERY JEWEL CLAIMED", PEN_BLACK);
        if (!g.relaxed) {
            sprintf(tmp, "TIME BONUS  %d", g.bonus);
            ctext(17, tmp, PEN_PALE);
        }
    } else {
        bar(10, 4, PEN_RED);
        big(13, 11, "TIME UP", PEN_ON_RED);
        sprintf(tmp, "JEWELS LEFT  %d", g.jewels_left);
        ctext(17, tmp, PEN_PALE);
    }
    sprintf(tmp, "FINAL SCORE  %06d", g.score);
    ctext(19, tmp, PEN_WHITE);
}

static void end_prompt(void)
{
    ctext(25, g.state_timer == 0 && ((g.tick / 15) & 1) ? "PRESS SPACE TO RETURN" : "                     ", PEN_GOLD);
}

static void draw_entry(void)
{
    uint8_t k;
    char c[2] = { 0, 0 };
    tm_fill(4, 21, 32, 6, 0, 0);
    ctext(21, "A HIGH SCORE! YOUR INITIALS:", PEN_GREEN);
    for (k = 0; k < 3; k++) {
        c[0] = ed.name[k];
        text(18 + k * 2, 23, c, k == ed.pos ? PEN_GOLD : PEN_WHITE);
        text(18 + k * 2, 24, k == ed.pos ? "-" : " ", PEN_GOLD);
    }
    ctext(26, "UP/DOWN OR TYPE, FIRE", PEN_PALE);
}

static const char *const mode_names[] = { "TITLE", "PLAYING", "WIN", "OVER" };

static void enter_mode(void)
{
    shown_mode = g.mode;
    zx_log("wk: state %s score=%d jewels=%u time=%d relaxed=%u\n", mode_names[g.mode], g.score, g.jewels_left,
           (g.time_left + 49) / 50, g.relaxed);
    if (g.mode == GM_PLAYING) {
        tm_clear();
        hud_score = hud_secs = -1;
        hud_jewels = -1;
        room_visible(1);
        draw_room();
        snd_music(MUS_PLAY);
    } else {
        room_visible(0);
        if (g.mode == GM_TITLE) {
            title_page = 0;
            title_timer = 0;
            draw_title();
            snd_music(MUS_TITLE);
        } else {
            draw_end();
            snd_music(MUS_NONE);
        }
    }
}

/* ---- input: the original's main.cpp ---- */
static uint8_t prev_input, held_dirs, held_frames, quit;

static uint8_t read_input(void)
{
    uint8_t in = input_read(), f = 0;
    quit = in & IN_QUIT;
    if (in & IN_LEFT) f |= INPUT_LEFT;
    if (in & IN_RIGHT) f |= INPUT_RIGHT;
    if (in & IN_UP) f |= INPUT_UP;
    if (in & IN_DOWN) f |= INPUT_DOWN;
    if (in & (IN_FIRE | IN_START)) f |= INPUT_START;
    return f;
}

static uint8_t tick_input(void)
{
    uint8_t input = read_input(), edge = input & ~prev_input, dirs, edge_dirs, out = 0;
    prev_input = input;
    dirs = input & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN);
    edge_dirs = edge & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN);
    if (edge_dirs) {                     /* an edge moves at once and starts the repeat clock */
        out |= edge_dirs;
        held_dirs = dirs;
        held_frames = 0;
    } else if (dirs && dirs == held_dirs) {
        if (held_frames < 255) held_frames++;
        if (held_frames == HOLD_DELAY || (held_frames > HOLD_DELAY && (held_frames - HOLD_DELAY) % REPEAT_EVERY == 0))
            out |= dirs;
    } else {
        held_dirs = dirs;
        held_frames = 0;
    }
    if (edge & INPUT_START) out |= INPUT_START;
    return out;
}

static void events(void)
{
    if (g.events & EV_STEP) zx_log("wk: player %d %d facing=%u\n", g.player.wx, g.player.wy, g.player.facing);
    if (g.events & EV_PUSH) { snd_play(SFX_PUSH); zx_log("wk: push\n"); }
    else if (g.events & EV_STEP) snd_play(SFX_STEP);
    if (g.events & EV_BUMP) snd_play(SFX_BUMP);
    if (g.events & EV_JEWEL) {
        snd_play(g.events & EV_WIN ? SFX_WIN : SFX_JEWEL);
        zx_log("wk: jewel score=%d left=%u\n", g.score, g.jewels_left);
    }
    if (g.events & EV_TIMEOUT) snd_play(SFX_TIMEOUT);
}

int main(void)
{
    uint8_t frames, k;
    uint16_t perf_frames = 0, perf_loops = 0;
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x13);           /* sprites on, over the border; order U S L */
    pal_load(1, room_pal, ROOM_COLOURS);
    pal_load(2, spr_pal, SPR_COLOURS);
    pal_set8(0, 16, L2_TRANSPARENT);     /* the border: the fallback colour (black) */
    zx_border(INK_BLACK);
    spr_patterns_from_pages(SPR_BANK * 2, 2, 0, SPR_PATTERNS, 10);
    text_init();
    l2w_init(ROOM_BANK);
    snd_init();
    input_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    hs_init(40);
    wk_init(&g);
    zx_log("wk: ready kempston=%u\n", input_kempston);
    for (;;) {
        frames = frame_wait();
        for (k = 0; k < frames; k++) snd_tick();
        perf_frames += frames;
        perf_loops++;
        if (perf_frames >= 100) {
            zx_log("wk: perf loops=%u frames=%u ay=%u,%u,%u,%u\n", perf_loops, perf_frames, snd_reg(0, 7),
                   snd_reg(0, 8) & 15, snd_reg(1, 7), snd_reg(1, 8) & 15);
            perf_frames = perf_loops = 0;
        }
        if (entering) {                  /* port: the initials */
            uint8_t r;
            input_read();
            r = hs_edit_step(&ed);
            if (r & HS_EDIT_TAKEN) snd_play(SFX_KEY);
            if (r & HS_EDIT_CHANGED) draw_entry();
            if (r & HS_EDIT_DONE) {
                hs_insert(ed_place, ed.name, (uint32_t)g.score);
                zx_log("wk: hiscore %s %d place=%d\n", ed.name, g.score, ed_place + 1);
                entering = 0;
                prev_input = INPUT_START;  /* the fire that ended the name doesn't start a game */
                g.mode = GM_TITLE;
                enter_mode();
                title_page = 1;
                draw_title();
            }
            continue;
        }
        {
            uint8_t in = tick_input();
            uint8_t was = g.mode;
            if (quit && was != GM_TITLE) {   /* BREAK: back to the title */
                g.mode = GM_TITLE;
                enter_mode();
                continue;
            }
            /* port: from an end screen, a score that makes the table goes to the initials first */
            if ((was == GM_WIN || was == GM_OVER) && (in & INPUT_START) && g.state_timer == 0 && !g.relaxed &&
                (ed_place = hs_rank((uint32_t)g.score)) >= 0) {
                hs_edit_begin(&ed);
                entering = 1;
                draw_entry();
                continue;
            }
            wk_tick(&g, in);
        }
        events();
        if (g.mode != shown_mode) enter_mode();
        if (g.mode == GM_PLAYING) {
            if (g.moved) {
                g.moved = 0;
                draw_room();
            }
            hud();
        } else if (g.mode == GM_TITLE) {
            uint8_t r = key_down(IN_KEY_SCANCODE_r);
            if (r && !mode_key) {              /* port: R, timed or relaxed */
                g.relaxed ^= 1;
                zx_log("wk: mode %s\n", g.relaxed ? "relaxed" : "timed");
                title_page = 0;
                title_timer = 0;
                draw_title();
            }
            mode_key = r;
            if (++title_timer == 300) {  /* port: the title and the high scores in turn */
                title_timer = 0;
                title_page ^= 1;
                draw_title();
            }
            title_blink();
        } else end_prompt();
    }
    return 0;
}
