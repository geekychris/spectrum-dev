/*
 * front.c - Tank Duel's shell (port of the original's game.c): the title
 * with the autopilot fighting behind it, the fight, game over, the help
 * pages, the explosions, messages and sounds for the rules' events, the
 * HUD and the radar.
 *
 * port: text, the HUD's bars and the boxes are on the Next's tilemap (over
 * the 3D view, the border round it too), so the picture never redraws
 * them; the radar's disc is drawn with the 3D view and its blips are
 * hardware sprites; the sounds are TurboSound effects modelled on the
 * original's synthesised samples (it has no music); there is a high-score
 * table with initials (the original keeps the best score only); keys:
 * Q/A/O/P or a joystick (the original: W/S/A/D), S pauses (P is "right").
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include <input.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "palfx.h"                       /* RGB9 */
#include "fixmath.h"
#include "td.h"
#include "duel.h"
#include "front.h"
#include "shell.h"

/* ------------------------------------------------------------------ the tilemap */

/* tiles: 0 empty, 1 the panel, 2..9 box edges, 10..18 bars of 0..8 pixels,
   32..127 the ROM font with a drop shadow, 128..223 the font on the panel,
   224.. big letters (4 tiles each) */
#define T_PANEL 1
#define T_BAR   10
#define T_PANELTEXT 128
#define T_BIG   224
static const char big_letters[] = "TANKDUEL";
/* palette offsets: the text's colour */

static uint8_t tile[32];

static void put_px(uint8_t x, uint8_t y, uint8_t pen)
{
    uint8_t *b = &tile[y * 4 + (x >> 1)];
    if (x & 1) *b = (*b & 0xF0) | pen;
    else *b = (*b & 0x0F) | (pen << 4);
}

/* font: the ROM's (copied by main.c to FONT_COPY before the banks went in) */
void front_ui_init(const uint8_t *font)
{
    uint8_t c, x, y, k;
    static uint8_t g[8];
    static const uint16_t fg[8] = { RGB9(7, 7, 7), RGB9(7, 7, 2), RGB9(3, 7, 3), RGB9(7, 1, 1),
                                    RGB9(2, 7, 7), RGB9(5, 5, 5), RGB9(7, 5, 1), RGB9(2, 2, 2) };
    tm_init();
    memset(tile, 0, 32);
    tm_def(0, tile, 1);
    memset(tile, 0x33, 32);
    tm_def(T_PANEL, tile, 1);
    for (k = 2; k < 10; k++) {                  /* box edges: top, bottom, left, right, corners */
        memset(tile, 0x33, 32);
        for (x = 0; x < 8; x++) {
            if (k == 2 || k == 6 || k == 7) put_px(x, 1, 1);
            if (k == 3 || k == 8 || k == 9) put_px(x, 6, 1);
            if (k == 4 || k == 6 || k == 8) put_px(1, x, 1);
            if (k == 5 || k == 7 || k == 9) put_px(6, x, 1);
        }
        tm_def(k, tile, 1);
    }
    for (k = 0; k < 9; k++) {                   /* bars: k pixels lit of 8, on the dark pen */
        memset(tile, 0x44, 32);
        for (y = 1; y < 7; y++) for (x = 0; x < k; x++) put_px(x, y, 1);
        tm_def(T_BAR + k, tile, 1);
    }
    for (c = 0; c < 96; c++) {
        memcpy(g, font + c * 8, 8);
        memset(tile, 0, 32);
        for (y = 0; y < 7; y++)
            for (x = 0; x < 7; x++)
                if (g[y] & (0x80 >> x)) put_px(x + 1, y + 1, 2);
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                if (g[y] & (0x80 >> x)) put_px(x, y, 1);
        tm_def(32 + c, tile, 1);
        memset(tile, 0x33, 32);
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                if (g[y] & (0x80 >> x)) put_px(x, y, 1);
        tm_def(T_PANELTEXT + c, tile, 1);
    }
    for (k = 0; k < 8; k++) {
        memcpy(g, font + (big_letters[k] - 32) * 8, 8);
        tm_glyph2x(T_BIG + k * 4, g, 1, 2);
    }
    for (k = 0; k < 16; k++) {
        tm_pal(k, 1, fg[k & 7]);
        tm_pal(k, 2, RGB9(0, 0, 0));
        tm_pal(k, 3, RGB9(1, 1, 2));            /* the panel */
        tm_pal(k, 4, RGB9(0, 0, 0));            /* the bars' trough */
    }
}

static void text(uint8_t col, uint8_t row, const char *s, uint8_t c) { tm_print(col, row, s, 32, c); }
static void ptext(uint8_t col, uint8_t row, const char *s, uint8_t c) { tm_print(col, row, s, T_PANELTEXT, c); }

static uint8_t slen(const char *s) { uint8_t n = 0; while (s[n]) n++; return n; }

/* centred on the 320-pixel tilemap */
static void centre(uint8_t row, const char *s, uint8_t c) { text(20 - slen(s) / 2, row, s, c); }
static void pcentre(uint8_t row, const char *s, uint8_t c) { ptext(20 - slen(s) / 2, row, s, c); }

static void clear_rows(uint8_t row, uint8_t n) { tm_fill(0, row, 40, n, 0, 0); }

static char *num(char *s, int32_t v)
{
    char t[12];
    uint8_t n = 0;
    if (v < 0) { *s++ = '-'; v = -v; }
    do { t[n++] = '0' + (char)(v % 10); v /= 10; } while (v);
    while (n) *s++ = t[--n];
    *s = 0;
    return s;
}

static void big(uint8_t col, uint8_t row, const char *s, uint8_t c)
{
    for (; *s; s++, col += 2) {
        const char *p = big_letters;
        uint8_t k = 0;
        while (*p && *p != *s) { p++; k++; }
        if (!*p) continue;
        tm_put(col, row, T_BIG + k * 4, c);
        tm_put(col + 1, row, T_BIG + k * 4 + 1, c);
        tm_put(col, row + 1, T_BIG + k * 4 + 2, c);
        tm_put(col + 1, row + 1, T_BIG + k * 4 + 3, c);
    }
}

static void box(uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t c)
{
    tm_fill(col, row, w, h, T_PANEL, c);
    tm_fill(col + 1, row, w - 2, 1, 2, c);
    tm_fill(col + 1, row + h - 1, w - 2, 1, 3, c);
    tm_fill(col, row + 1, 1, h - 2, 4, c);
    tm_fill(col + w - 1, row + 1, 1, h - 2, 5, c);
    tm_put(col, row, 6, c);
    tm_put(col + w - 1, row, 7, c);
    tm_put(col, row + h - 1, 8, c);
    tm_put(col + w - 1, row + h - 1, 9, c);
}

/* text on the panel (tiles 32.. drawn on pen 0: put the panel round it) */
static void bar(uint8_t col, uint8_t row, uint8_t cells, uint16_t px, uint8_t c)
{
    uint8_t k;
    for (k = 0; k < cells; k++) {
        uint8_t v = px >= 8 ? 8 : (uint8_t)px;
        tm_put(col + k, row, T_BAR + v, c);
        px = px >= 8 ? px - 8 : 0;
    }
}

static hs_editor ed;
static int8_t ed_place;

/* the keys beyond the joystick's (the original's P, B, HELP, ESC) */
#define K_HELP  1
#define K_AUTO  2
#define K_PAUSE 4

/* once a frame, before the ticks */
void front_input(void)
{
    uint8_t in = input_read(), pr = input_pressed(), edge = 0;
    char c = front_entering ? 0 : input_letter_pressed();   /* latched: a tap during a slow frame counts */
    if (c == 'H') edge = K_HELP;
    else if (c == 'B') edge = K_AUTO;
    else if (c == 'S') edge = K_PAUSE;
    if (front_entering) {
        uint8_t r = hs_edit_step(&ed);
        if (r & HS_EDIT_TAKEN) sfx(SFX_BOUNCE);
        if (r & HS_EDIT_CHANGED) hud_dirty = 1;
        if (r & HS_EDIT_DONE) {
            hs_insert(ed_place, ed.name, (uint32_t)game.score);
            zx_log("td: name=%s\n", ed.name);
            front_entering = 0;
            to_title();
        }
        return;
    }
    if (edge & K_HELP) {
        du.help = (du.help + 1) % 3;
        front_redraw();
        return;
    }
    if (du.help) {
        if (pr & (IN_FIRE | IN_START | IN_QUIT)) { du.help = 0; front_redraw(); }
        return;
    }
    if ((pr & IN_QUIT) && du.state != DS_TITLE) {
        to_title();
        return;
    }
    play_in = 0;
    switch (du.state) {
    case DS_TITLE:
        if ((pr & IN_UP) && du.start_round < TD_ROUNDS) { du.start_round++; hud_dirty = 1; }
        if ((pr & IN_DOWN) && du.start_round > 1) { du.start_round--; hud_dirty = 1; }
        if (pr & (IN_FIRE | IN_START)) start_game(du.start_round);
        break;
    case DS_PLAY:
        if (edge & K_PAUSE) { du.paused = !du.paused; hud_dirty = 1; }
        if (edge & K_AUTO) { du.autoplay = !du.autoplay; hud_dirty = 1; zx_log("td: autoplay=%u\n", du.autoplay); }
        play_in = (in & IN_LEFT ? TD_IN_LEFT : 0) | (in & IN_RIGHT ? TD_IN_RIGHT : 0) | (in & IN_UP ? TD_IN_UP : 0) |
                  (in & IN_DOWN ? TD_IN_DOWN : 0) | ((in & IN_FIRE) && !fire_lock ? TD_IN_FIRE : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case DS_OVER:
        if (du.state_timer == 0 && (pr & (IN_FIRE | IN_START))) {
            ed_place = hs_rank((uint32_t)game.score);
            if (ed_place >= 0) {
                hs_edit_begin(&ed);
                front_entering = 1;
                front_redraw();
                zx_log("td: hiscore rank=%d\n", ed_place);
            } else to_title();
        }
        break;
    }
}

/* ------------------------------------------------------------------ the screens (tilemap) */

/* tilemap rows: 0-3 the top border, 4-27 the 3D view, 28-31 the bottom border */
static int32_t shown_score = -1;
static int16_t shown_round = -1, shown_enemies = -1, shown_hp = -1, shown_reload = -1;
static uint8_t shown_flash, shown_between, shown_start;

void front_redraw(void)
{
    tm_clear();
    shown_score = -1;
    shown_round = shown_enemies = shown_hp = shown_reload = -1;
    shown_flash = shown_between = shown_start = 0;
    hud_dirty = 1;
}

static const char *const help_lines[2][12] = {
    { "DESTROY EVERY ENEMY TANK IN THE ARENA.",
      "SHELLS BOUNCE ONCE OFF THE WALLS: BANK",
      "A SHOT ROUND COVER. CRATES BREAK.",
      "",
      " SCOUT    QUICK, CIRCLES AND SNIPES",
      " MORTAR   LOBS SHELLS AT WHERE YOU ARE",
      "          - GET OUT OF THE RED RING",
      " BULWARK  SHIELDED IN FRONT: SHOTS",
      "          GLANCE OFF. IT TURNS SLOWLY,",
      "          SO GET ROUND TO ITS SIDE.",
      "",
      "8 ROUNDS. +30 ARMOUR BETWEEN THEM." },
    { "Q/A, JOYSTICK   FORWARD / BACK",
      "O/P, JOYSTICK   TURN",
      "SPACE / FIRE    FIRE",
      "S               PAUSE",
      "B               AUTOPILOT",
      "H               THESE PAGES",
      "BREAK           TITLE",
      "",
      "SCOUT 100, MORTAR 200, BULWARK 300,",
      "CRATE 20. ROUND 500 x ROUND, AND 5",
      "FOR EACH POINT OF ARMOUR LEFT.",
      "THE RADAR: AHEAD IS UP." },
};

static void draw_help(void)
{
    uint8_t k;
    box(0, 0, 40, 32, C_WHITE);
    pcentre(2, du.help == 1 ? "HOW TO PLAY" : "CONTROLS", C_YELLOW);
    for (k = 0; k < 12; k++) ptext(1, 5 + k * 2, help_lines[du.help - 1][k], C_WHITE);
    ptext(1, 30, du.help == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", C_GREY);
    ptext(17, 30, "H: NEXT  FIRE: PLAY", C_YELLOW);
}

static void draw_title(void)
{
    char s[40], *p;
    uint8_t k;
    big(14, 5, "TANK", C_YELLOW);
    big(18, 8, "DUEL", C_ORANGE);
    box(6, 17, 28, 13, C_WHITE);
    pcentre(18, "HIGH SCORES", C_YELLOW);
    for (k = 0; k < HS_ENTRIES; k++) {
        p = s;
        *p++ = '1' + k; *p++ = ' '; *p++ = ' ';
        memcpy(p, hs_table[k].name, 3); p += 3;
        *p++ = ' '; *p++ = ' ';
        num(p, (int32_t)hs_table[k].score);
        ptext(13, 19 + k, s, C_WHITE);
    }
    memcpy(s, "START AT ROUND ", 15);
    p = num(s + 15, du.start_round);
    memcpy(p, " (UP/DOWN)", 11);
    pcentre(25, s, C_GREEN);
    pcentre(27, "H: HOW TO PLAY", C_GREY);
}

static void draw_entry(void)
{
    char s[24];
    box(8, 9, 24, 12, C_WHITE);
    pcentre(10, "NEW HIGH SCORE", C_YELLOW);
    num(s, game.score);
    pcentre(12, s, C_WHITE);
    pcentre(14, "TYPE YOUR INITIALS", C_GREY);
    pcentre(15, "OR UP/DOWN + FIRE", C_GREY);
    ptext(18, 18, ed.name, C_GREEN);
    tm_fill(18, 19, 3, 1, 0, 0);
    if (ed.pos < 3) tm_put(18 + ed.pos, 19, '^', C_YELLOW);
}

/* (td_enemies_left is in the rules' bank, at the addresses this one has) */
static int16_t enemies(void)
{
    uint8_t k;
    int16_t n = 0;
    for (k = 1; k < game.ntanks; k++) if (game.tk[k].alive) n++;
    return n;
}

static void draw_play(void)
{
    char s[40], *p;
    const Tank *pl = &game.tk[0];
    if (game.round != shown_round) {
        memcpy(s, "ROUND ", 6);
        num(s + 6, game.round);
        text(4, 2, s, C_YELLOW);
        shown_round = game.round;
    }
    if (enemies() != shown_enemies) {
        shown_enemies = (int16_t)enemies();
        memcpy(s, "ENEMIES ", 8);
        p = num(s + 8, shown_enemies);
        *p++ = ' '; *p = 0;
        text(14, 2, s, C_WHITE);
    }
    if (game.score != shown_score) {
        shown_score = game.score;
        num(s, game.score);
        tm_fill(26, 2, 10, 1, 0, 0);
        text(36 - slen(s), 2, s, C_GREY);
    }
    if (pl->hp != shown_hp) {
        shown_hp = (int16_t)pl->hp;
        text(4, 29, "ARMOUR", C_GREY);
        bar(11, 29, 10, (uint16_t)pl->hp * 80 / TD_HP, pl->hp > 40 ? C_GREEN : C_RED);
    }
    if (pl->reload != shown_reload) {
        shown_reload = (int16_t)pl->reload;
        text(24, 29, "GUN", C_GREY);
        bar(28, 29, 8, (uint16_t)(TD_RELOAD - pl->reload) * 64 / TD_RELOAD, pl->reload ? C_ORANGE : C_YELLOW);
    }
    if (!hud_dirty) return;
    hud_dirty = 0;
    /* the messages in the view */
    clear_rows(8, 12);
    if (du.autoplay) text(4, 30, "AUTO", C_GREEN); else tm_fill(4, 30, 4, 1, 0, 0);
    if (du.flash_timer > 0 && du.flash[0]) centre(9, du.flash, C_WHITE);
    if (game.between) {
        centre(12, "ROUND CLEAR", C_GREEN);
        if (game.round < TD_ROUNDS) {
            memcpy(s, "ROUND ", 6);
            p = num(s + 6, game.round + 1);
            memcpy(p, " NEXT", 6);
            centre(14, s, C_WHITE);
        }
    } else if (du.state == DS_PLAY && game.tick < 50) {
        memcpy(s, "ROUND ", 6);
        p = num(s + 6, game.round);
        memcpy(p, ": ", 2);
        p = num(p + 2, enemies());
        memcpy(p, " ENEMIES", 9);
        centre(11, s, C_YELLOW);
    }
    if (du.state == DS_OVER) {
        box(8, 10, 24, 9, game.won ? C_GREEN : C_RED);
        pcentre(11, game.won ? "VICTORY" : "GAME OVER", game.won ? C_GREEN : C_RED);
        p = num(s, game.kills);
        memcpy(p, " KILLS  ", 8);
        p = num(p + 8, game.shots ? (int32_t)game.hits * 100 / game.shots : 0);
        memcpy(p, "% HITS", 7);
        pcentre(13, s, C_YELLOW);
        memcpy(s, "SCORE ", 6);
        num(s + 6, game.score);
        pcentre(15, s, C_WHITE);
        if (du.state_timer == 0) pcentre(17, "PRESS FIRE", C_YELLOW);
    }
    if (du.paused) centre(18, "PAUSED", C_WHITE);
}

/* once a frame, after the picture */
void front_screen(void)
{
    if (du.help) {
        if (hud_dirty) { hud_dirty = 0; draw_help(); }
        return;
    }
    if (front_entering) {
        if (hud_dirty) { hud_dirty = 0; draw_entry(); }
        return;
    }
    if (du.state == DS_TITLE) {
        if (hud_dirty) { hud_dirty = 0; draw_title(); }
        return;
    }
    draw_play();
}

/* ------------------------------------------------------------------ the radar's blips (sprites) */

/* sprite patterns 0..5: a 3 x 3 dot (1 x 1 for cover) in each blip colour */
void front_sprites_init(void)
{
    static uint8_t pat[256];
    static const uint8_t pens[6] = { 3, PEN_RED, PEN_GREEN, PEN_FIRE, PEN_CYAN, 1 };
    uint8_t k, x, y;
    for (k = 0; k < 6; k++) {
        memset(pat, L2_TRANSPARENT, 256);
        for (y = 0; y < 3; y++)
            for (x = 0; x < 3; x++)
                if (k || (x == 1 && y == 1)) pat[(7 + y) * 16 + 7 + x] = pens[k];
        if (k == 5) for (y = 0; y < 5; y++) pat[(3 + y) * 16 + 8] = 1;   /* the player: a dot and "ahead" */
        spr_pattern(k, pat);
    }
}

#define RADAR_CX 220                    /* Layer 2 coordinates (render.c draws the disc) */
#define RADAR_CY 30
#define RADAR_R  26
static uint8_t blips_shown;

static void blip(uint8_t *n, fix wx, fix wz, uint8_t pat, int16_t c, int16_t s)
{
    const Tank *p = &game.tk[0];
    int16_t dx = (int16_t)((wx - p->x) >> 16), dz = (int16_t)((wz - p->z) >> 16), bx, by;
    /* turned so ahead is up; 1 pixel = 50 units (the original's 40 at 0.8) */
    int16_t rx = (int16_t)((mul16(dx, c) - mul16(dz, s)) >> 14), rz = (int16_t)((mul16(dx, s) + mul16(dz, c)) >> 14);
    bx = rx / 50;
    by = -rz / 50;
    if (mul16(bx, bx) + mul16(by, by) >= RADAR_R * RADAR_R) return;
    spr_show(*n, RADAR_CX + bx + 32 - 8, RADAR_CY + by + 32 - 8, pat, 0);
    (*n)++;
}

void front_radar(void)
{
    uint8_t n = 0, k;
    int16_t c, s;
    const TDGame *g = &game;
    if (du.state == DS_PLAY || du.state == DS_OVER) {
        uint16_t yaw = (uint16_t)g->tk[0].yaw;
        c = fix_sin14((uint8_t)((yaw >> 8) + 64));
        s = fix_sin14((uint8_t)(yaw >> 8));
        for (k = 0; k < g->nobs; k++)
            if (g->ob[k].kind != OB_CRATE || g->ob[k].hp > 0) blip(&n, g->ob[k].x, g->ob[k].z, 0, c, s);
        for (k = 0; k < TD_MAX_MORTARS; k++)
            if (g->mo[k].alive) blip(&n, g->mo[k].tx, g->mo[k].tz, 1, c, s);
        for (k = 1; k < g->ntanks; k++)
            if (g->tk[k].alive) blip(&n, g->tk[k].x, g->tk[k].z, g->tk[k].type == TK_SCOUT ? 2 : g->tk[k].type == TK_MORTAR ? 3 : 4, c, s);
        spr_show(n++, RADAR_CX + 32 - 8, RADAR_CY + 32 - 8, 5, 0);
    }
    for (k = n; k < blips_shown; k++) spr_hide(k);
    blips_shown = n;
}
