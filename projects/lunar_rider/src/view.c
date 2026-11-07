/*
 * Lunar Rider - the Spectrum Next's picture (port: replaces draw.c).
 *
 * The original's 320 x 256 screen is the Next's 320 x 256 Layer 2, pixel
 * for pixel. Its parallax layers are hardware layers:
 *
 *   tilemap (bottom)  the sky gradient and the far mountains: their profile
 *                     repeats every 320 pixels, so they scroll by the
 *                     tilemap's scroll register alone, at 1/4 speed
 *   Layer 2           three bands (sdk l2scroll), each with its own X scroll
 *                     set by the copper at its top row:
 *                       rows 0..79     static: stars, the HUD, messages
 *                       rows 80..175   the near mountains, 1/2 speed
 *                       rows 176..255  the ground, full speed
 *                     each a ring of 320 columns: scrolling draws only the
 *                     columns that come into view (1-3 a frame)
 *   sprites (top)     the buggy, UFOs, meteors, bombs, bullets, explosions
 *
 * Text that has to sit over a scrolling band (GAME OVER) is drawn while the
 * scrolling has stopped, at the bands' current offsets.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "game.h"
#include "view.h"
#include "zxgame.h"
#include "nextgfx.h"
#include "nextcopper.h"
#include "nextsplit.h"
#include "nexttile.h"
#include "l2scroll.h"
#include "font5.h"
#include "gfx_data.h"

extern GameState gs;

#define L2_BANK 16                      /* Layer 2: banks 16-20 */
#define TR L2_TRANSPARENT

/* colours (RRRGGGBB): the original's palette and shades of it */
#define C_WHITE   0xFF
#define C_BRYEL   0xFC                  /* 0xFF0 */
#define C_YELLOW  0xF8                  /* 0xFD0 */
#define C_GREEN   0x1C
#define C_RED     0xE0
#define C_DKGRAY  0x49
#define C_GRAY    0xB6
#define C_PURPLE  0x46                  /* 0x426 */
#define C_SKYTXT  0x6F

#define BAND_A_Y1 79
#define BAND_B_Y0 80
#define BAND_B_Y1 175
#define BAND_C_Y0 176
static uint8_t band_a, band_b, band_c;

/* ------------------------------------------------------------ stars */

static uint16_t star_x[30];
static uint8_t star_y[30];

/* draw_stars(): the same 30 stars (seed 42), in the top 80 rows */
static void stars_init(void)
{
    uint32_t seed = 42;
    uint8_t i;
    for (i = 0; i < 30; i++) {
        seed = seed * 1103515245UL + 12345UL;
        star_x[i] = (uint16_t)((seed >> 16) % SCREEN_W);
        star_y[i] = (uint8_t)((seed >> 8) % 80);
    }
}

static void stars_column(uint16_t x, uint8_t *col, uint8_t y0, uint8_t y1)
{
    uint8_t i;
    for (i = 0; i < 30; i++)
        if (star_x[i] == x && star_y[i] >= y0 && star_y[i] <= y1)
            col[star_y[i]] = (i & 3) ? C_WHITE : 0xB7;   /* a few dimmer */
}

/* ------------------------------------------------------------ band fills */

static void fill_a(uint8_t b, uint32_t wx, uint8_t *col)
{
    (void)b;
    memset(col, TR, BAND_A_Y1 + 1);
    stars_column((uint16_t)wx, col, 0, BAND_A_Y1);
}

/* the near mountains (the original's purple 0x426): a lit rim, then the
   body, dithered darker over its last 16 rows (two column phases) */
#define NEAR_BODY 0x45
#define NEAR_DARK 0x25
static uint8_t near_body[2][BAND_B_Y1 - BAND_B_Y0 + 1];

static void fill_b(uint8_t b, uint32_t wx, uint8_t *col)
{
    uint8_t h = near_h[(uint16_t)wx & 511];
    (void)b;
    memset(col + BAND_B_Y0, TR, h - BAND_B_Y0);
    col[h] = 0x8E;
    col[h + 1] = 0x6A;
    memcpy(col + h + 2, near_body[wx & 1] + h + 2 - BAND_B_Y0, 170 - (h + 2) + 1);
    memset(col + 171, TR, BAND_B_Y1 - 171 + 1);
}

/* the ground: surface, regolith (8 textures by x), craters, rocks, mines */
static uint8_t ground_tex[8][256 - GROUND_Y];   /* rows GROUND_Y..255 */

static void ground_init(void)
{
    uint8_t v, y;
    for (v = 0; v < 8; v++)
        for (y = 0; y < 256 - GROUND_Y; y++) {
            uint8_t row = GROUND_Y + y, h = (uint8_t)(v * 37 + y * 11 + (y >> 2) * v);
            uint8_t c;
            if (row > SCREEN_H - 25) c = row > SCREEN_H - 22 ? 0x00 : 0x20;   /* the original's black below */
            else if ((h & 7) == 0) c = 0x64;                                  /* light grains */
            else if ((h & 7) < 3) c = 0x40;                                   /* dark */
            else c = 0x44;                                                    /* 0x520 */
            ground_tex[v][y] = c;
        }
}

static WORD ring(uint32_t wx) { return (WORD)(wx % TERRAIN_LEN); }

static void fill_c(uint8_t b, uint32_t wx, uint8_t *col)
{
    WORD i = ring(wx);
    uint8_t t = (uint8_t)gs.terrain[i], th = (uint8_t)gs.terrain_h[i], y;
    int8_t d;
    (void)b;
    if (t == TERRAIN_CRATER_SM || t == TERRAIN_CRATER_LG) {
        memset(col + BAND_C_Y0, TR, GROUND_Y - BAND_C_Y0);
        /* the original's black hole, 16 rows, and the ground under it */
        for (y = GROUND_Y; y < GROUND_Y + 16; y++) col[y] = y < GROUND_Y + 3 ? 0x24 : 0x00;
        memcpy(col + GROUND_Y + 16, ground_tex[wx & 7] + 16, 256 - GROUND_Y - 16);
        return;
    }
    memset(col + BAND_C_Y0, TR, th - BAND_C_Y0);
    col[th] = 0xAC;                     /* the surface (0x840), lit on top */
    col[th + 1] = 0x88;
    col[th + 2] = 0x84;
    for (y = th + 3; y < GROUND_Y; y++) col[y] = 0x44;   /* (hills) */
    y = th + 3 > GROUND_Y ? th + 3 : GROUND_Y;
    memcpy(col + y, ground_tex[wx & 7] + (y - GROUND_Y), 256 - y);
    /* port: rocks and mines are one terrain column in the original (drawn
       one pixel wide); here a boulder 5 wide and a mine 4 wide around it */
    for (d = -2; d <= 2; d++) {
        WORD j = ring(wx + TERRAIN_LEN - d);
        uint8_t k = (uint8_t)gs.terrain[j], top, ad = d < 0 ? -d : d;
        if (k == TERRAIN_ROCK) {
            top = th - (ad == 0 ? 10 : ad == 1 ? 9 : 6);
            for (y = top; y < th; y++)
                col[y] = y == top ? (d > 0 ? 0x92 : 0xB6) : (d > 0 ? 0x49 : d == 0 ? 0x6D : 0x92);
        }
        else if (k == TERRAIN_MINE && d >= -1 && d <= 2) {
            top = th - (d == -1 || d == 2 ? 2 : 4);
            for (y = top; y < th; y++) col[y] = y == top ? 0xF1 : 0xC0;
        }
    }
}

/* ------------------------------------------------------------ text */

static uint16_t num_str(char *buf, long v)
{
    char tmp[12];
    uint8_t n = 0, i = 0;
    if (v < 0) v = 0;
    do { tmp[n++] = '0' + (char)(v % 10); v /= 10; } while (v);
    while (n) buf[i++] = tmp[--n];
    buf[i] = 0;
    return i;
}

static uint16_t text_w(const char *s, uint8_t scale)
{
    uint16_t n = strlen(s);
    return n ? n * 6 * scale - scale : 0;
}

/* the original's draw_string_at(): 5 x 7 dots, `scale` pixels each, on a
   static part of the screen (band A, or the title) */
static void txt(uint16_t x, uint8_t y, const char *s, uint8_t c, uint8_t scale)
{
    l2w_text5(x, y, s, c, scale, scale, 0);
}

static void txt_center(uint8_t y, const char *s, uint8_t c, uint8_t scale)
{
    txt(SCREEN_W / 2 - text_w(s, scale) / 2, y, s, c, scale);
}

/* text over any band at screen position (x, y): each dot goes to the column
   the band shows there now (the scrolling has stopped: game over) */
static void text_screen(uint16_t x, uint8_t y, const char *s, uint8_t c, uint8_t scale)
{
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = font5_glyph(*s);
        uint8_t cx, r, j;
        if (!g) continue;
        for (cx = 0; cx < FONT5_W * scale; cx++) {
            uint16_t sx = x + cx;
            for (r = 0; r < FONT5_H; r++) {
                if (!(g[r] & (0x80 >> (cx / scale)))) continue;
                for (j = 0; j < scale; j++) {
                    uint8_t sy = y + r * scale + j, band = sy <= BAND_A_Y1 ? band_a : sy < BAND_C_Y0 ? band_b : band_c;
                    uint16_t col = sx + l2s_offset(band);
                    if (col >= L2S_W) col -= L2S_W;
                    l2w_column(col)[sy] = c;
                }
            }
        }
    }
    l2w_done();
}

/* clear a rectangle of band A back to stars */
static void clear_a(uint16_t x0, uint8_t y0, uint16_t x1, uint8_t y1)
{
    for (; x0 <= x1 && x0 < SCREEN_W; x0++) {
        uint8_t *col = l2w_column(x0);
        memset(col + y0, TR, y1 - y0 + 1);
        stars_column(x0, col, y0, y1);
    }
    l2w_done();
}

/* ------------------------------------------------------------ HUD */

static long hud_score = -1, hud_hi = -1;
static WORD hud_lives = -1, hud_cp = -1, hud_prog = -1;

static void hud_reset(void)
{
    hud_score = hud_hi = -1;
    hud_lives = hud_cp = hud_prog = -1;
}

/* draw_hud(): score, lives, zone and its progress bar, the high score */
static void hud(void)
{
    char buf[12];
    if (hud_score != gs.score) {
        clear_a(40, 2, 120, 8);
        num_str(buf, gs.score);
        txt(40, 2, buf, C_BRYEL, 1);
        hud_score = gs.score;
    }
    if (hud_lives != gs.lives) {
        WORD i;
        clear_a(40, 12, 92, 17);
        for (i = 0; i < gs.lives && i < 5; i++) l2w_fill(40 + i * 10, 12, 7, 6, C_YELLOW);
        hud_lives = gs.lives;
    }
    if (hud_cp != gs.checkpoint_cur) {
        WORD z = gs.checkpoint_cur + 1;
        char sec[8] = "ZONE 00";
        sec[5] = '0' + (char)(z / 10);
        sec[6] = '0' + (char)(z % 10);
        clear_a(SCREEN_W / 2 - 21, 2, SCREEN_W / 2 + 21, 8);
        txt(SCREEN_W / 2 - 21, 2, sec, C_GREEN, 1);
        hud_cp = gs.checkpoint_cur;
    }
    {   /* the checkpoint progress bar, 60 pixels */
        WORD progress = (WORD)(60 - (gs.checkpoint_dist * 60 / CHECKPOINT_DIST));
        if (progress < 0) progress = 0;
        if (progress > 60) progress = 60;
        if (progress != hud_prog) {
            l2w_fill(SCREEN_W / 2 - 30, 12, 61, 3, C_DKGRAY);
            if (progress > 0) l2w_fill(SCREEN_W / 2 - 30, 12, progress + 1, 3, C_GREEN);
            hud_prog = progress;
        }
    }
    /* port: the high score table's best (the original showed the score) */
    {
        long hi = (long)hs_table[0].score;
        if (gs.score > hi) hi = gs.score;
        if (hi != hud_hi) {
            clear_a(SCREEN_W - 56, 2, SCREEN_W - 1, 8);
            num_str(buf, hi);
            txt(SCREEN_W - 56, 2, buf, C_WHITE, 1);
            hud_hi = hi;
        }
    }
}

static void hud_labels(void)
{
    txt(4, 2, "SCORE", C_BRYEL, 1);
    txt(4, 12, "LIVES", C_WHITE, 1);
    txt(SCREEN_W - 72, 2, "HI", C_WHITE, 1);
}

/* ------------------------------------------------------------ messages */

static uint8_t cp_shown;

/* draw_checkpoint(): ZONE nn and 1000 BONUS (band A: static) */
static void checkpoint_msg(uint8_t on)
{
    if (on == cp_shown) return;
    clear_a(80, 30, 240, 57);
    if (on) {
        char msg[8] = "ZONE 00";
        WORD z = gs.checkpoint_cur + 1;
        msg[5] = '0' + (char)(z / 10);
        msg[6] = '0' + (char)(z % 10);
        txt_center(30, msg, C_GREEN, 2);
        txt_center(50, "1000 BONUS", C_BRYEL, 1);
    }
    cp_shown = on;
}

void view_gameover(void)
{
    char buf[12];
    num_str(buf, gs.score);
    text_screen(SCREEN_W / 2 - text_w("GAME OVER", 3) / 2, SCREEN_H / 2 - 30, "GAME OVER", C_RED, 3);
    text_screen(SCREEN_W / 2 - text_w(buf, 2) / 2, SCREEN_H / 2 + 10, buf, C_BRYEL, 2);
}

/* ------------------------------------------------------------ sprites */

/* sprite numbers */
#define S_BUGGY   0
#define S_ENEMY   2                     /* 2 each */
#define S_BOMB    (S_ENEMY + 2 * MAX_ENEMIES)
#define S_FWD     (S_BOMB + MAX_BOMBS)
#define S_UP      (S_FWD + MAX_FWD_BULLETS)
#define S_EXPL    (S_UP + MAX_UP_BULLETS)   /* 4 each */
#define S_END     (S_EXPL + 4 * MAX_EXPLOSIONS)

static void sprites(void)
{
    uint8_t i;
    Buggy *b = &gs.buggy;
    if (b->alive && gs.state != STATE_TITLE) {
        uint8_t p = P_BUGGY0 + (b->wheel_frame ? 2 : 0);
        spr_show(S_BUGGY, b->x, b->y - 5, p, 0);
        spr_show(S_BUGGY + 1, b->x + 16, b->y - 5, p + 1, 0);
    }
    else {
        spr_hide(S_BUGGY);
        spr_hide(S_BUGGY + 1);
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs.enemies[i];
        uint8_t s = S_ENEMY + i * 2, fl = (e->flash & 8) ? 1 : 0;
        if (!e->active) { spr_hide(s); spr_hide(s + 1); continue; }
        if (e->type == ENEMY_UFO_LG) {
            spr_show(s, e->x, e->y, P_UFOL0 + fl * 2, 0);
            spr_show(s + 1, e->x + 16, e->y, P_UFOL0 + fl * 2 + 1, 0);
        }
        else {
            if (e->type == ENEMY_UFO_SM) spr_show(s, e->x, e->y, P_UFOS0 + fl, 0);
            else spr_show(s, e->x, e->y - 2, P_METEOR, 0);
            spr_hide(s + 1);
        }
    }
    for (i = 0; i < MAX_BOMBS; i++) {
        Bomb *m = &gs.bombs[i];
        if (m->active) spr_show(S_BOMB + i, m->x - 1, m->y - 1, P_BOMB, 0);
        else spr_hide(S_BOMB + i);
    }
    for (i = 0; i < MAX_FWD_BULLETS; i++) {
        Bullet *u = &gs.fwd_bullets[i];
        if (u->active) spr_show(S_FWD + i, u->x, u->y, P_BULLET_F, 0);
        else spr_hide(S_FWD + i);
    }
    for (i = 0; i < MAX_UP_BULLETS; i++) {
        Bullet *u = &gs.up_bullets[i];
        if (u->active) spr_show(S_UP + i, u->x, u->y, P_BULLET_U, 0);
        else spr_hide(S_UP + i);
    }
    /* draw_explosions_all(): a square of radius r, as four mirrored quarters */
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *x = &gs.explosions[i];
        uint8_t s = S_EXPL + i * 4, st, p;
        if (!x->active) { spr_hide(s); spr_hide(s + 1); spr_hide(s + 2); spr_hide(s + 3); continue; }
        st = x->life <= 6 ? 6 : (uint8_t)(x->radius / 2 - 1);
        if (st > 6) st = 6;
        p = P_EXPL + st;
        spr_show(s, x->x - 15, x->y - 15, p, 0);
        spr_show(s + 1, x->x, x->y - 15, p, SPR_MIRROR_X);
        spr_show(s + 2, x->x - 15, x->y, p, SPR_MIRROR_Y);
        spr_show(s + 3, x->x, x->y, p, SPR_MIRROR_X | SPR_MIRROR_Y);
    }
}

void view_hide_sprites(void)
{
    uint8_t i;
    for (i = 0; i < S_END + 2; i++) spr_hide(i);
}

/* ------------------------------------------------------------ copper */

static uint16_t far_off;

/* the bands' scroll registers for this frame: uploaded first thing after the
   frame interrupt, while the beam is in the border above the picture */
static void copper_bands(void)
{
    split_begin();
    l2s_copper();
    split_end();
}

static void copper_still(void)
{
    copper_begin();
    copper_move(0x16, 0);
    copper_move(0x71, 0);
    copper_end();
}

/* ------------------------------------------------------------ setup */

static uint32_t ground_x(void) { return (uint32_t)gs.scroll_x / 100; }
static uint32_t near_x(void) { return (uint32_t)gs.scroll_x / 200; }

void view_init(void)
{
    uint8_t i;
    l2w_slot = 2;                       /* the program is in slot 3 (0x6000) */
    l2w_restore = 10;
    l2w_init(L2_BANK);
    l2w_fill(0, 0, SCREEN_W, SCREEN_H, TR);
    tm_init();
    tm_def(0, tile_defs, TILE_DEFS);
    {   /* a black tile after the generated ones (rows below the sky: every
           pixel of the display is covered by some layer) */
        static const uint8_t black[32] = {
            0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
            0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
        tm_def(TILE_DEFS, black, 1);
    }
    for (i = 0; i < TM_PENS; i++) tm_pal(0, i, tm_pens[i]);
    {
        uint8_t c, r;
        for (c = 0; c < TM_COLS; c++) {
            for (r = 0; r < SKY_ROWS; r++) tm_put(c, r, far_map[c][r], 0);
            for (; r < TM_ROWS; r++) tm_put(c, r, TILE_DEFS, 0);
        }
    }
    ZXN_WRITE_REG(0x4A, 0x00);          /* behind everything: black */
    for (i = 0; i < sizeof(near_body[0]); i++) {
        uint8_t y = BAND_B_Y0 + i, d = y < 155 ? 0 : y < 163 ? 1 : 2;   /* none, half, all dark */
        near_body[0][i] = d == 2 || (d == 1 && (y & 1)) ? NEAR_DARK : NEAR_BODY;
        near_body[1][i] = d == 2 || (d == 1 && !(y & 1)) ? NEAR_DARK : NEAR_BODY;
    }
    ground_init();
    stars_init();
    spr_patterns_from_pages(SPR_PAGE, 2, 0, SPR_COUNT, 10);
    l2s_reset();
    band_a = l2s_band(0, BAND_A_Y1, fill_a);
    band_b = l2s_band(BAND_B_Y0, BAND_B_Y1, fill_b);
    band_c = l2s_band(BAND_C_Y0, 255, fill_c);
}

/* the playfield for the game in gs: every band drawn at its position */
void view_play(void)
{
    view_hide_sprites();
    l2s_goto(band_a, 0);
    l2s_goto(band_b, near_x());
    l2s_goto(band_c, ground_x());
    far_off = 0xFFFF;
    hud_reset();
    hud_labels();
    cp_shown = 0;
    ZXN_WRITE_REG(0x6B, 0x81);          /* tilemap on */
    copper_bands();
}

/* one frame of the game */
void view_frame(void)
{
    uint8_t i;
    l2s_move(band_b, near_x());
    l2s_move(band_c, ground_x());
    for (i = 0; i < gs.n_dirty; i++) {       /* a rock shot, a mine gone */
        uint32_t gx = ground_x(), ri = gx % TERRAIN_LEN, d;
        WORD idx = gs.dirty[i];
        int8_t k;
        d = (uint32_t)(idx >= (WORD)ri ? idx - ri : idx + TERRAIN_LEN - ri);
        for (k = -2; k <= 2; k++) l2s_redraw(band_c, gx + d + k);
    }
    gs.n_dirty = 0;
    {
        /* the far mountains: 1/4 speed (the original's scroll_x / 400) */
        uint16_t f = (uint16_t)(((uint32_t)gs.scroll_x / 400) % 320);
        if (f != far_off) {
            tm_scroll(f, 0);
            far_off = f;
        }
    }
    hud();
    checkpoint_msg(gs.state == STATE_CHECKPOINT);
    sprites();
}

/* after frame_wait(): the copper list for the bands' new offsets */
void view_commit(void)
{
    copper_bands();
}

/* ------------------------------------------------------------ screens */

void view_title(uint8_t blink)
{
    uint16_t cx = SCREEN_W / 2;
    uint8_t i;
    view_hide_sprites();
    copper_still();
    tm_scroll(0, 0);
    ZXN_WRITE_REG(0x6B, 0x81);
    l2w_fill(0, 0, SCREEN_W, SCREEN_H, 0x00); /* the original's black title */
    clear_a(0, 0, SCREEN_W - 1, BAND_A_Y1);   /* the stars */
    txt_center(40, "LUNAR RIDER", 0x1F, 3);
    txt_center(75, "LUNAR DEFENSE FORCE", C_PURPLE | 0x20, 1);
    l2w_fill(0, 112, SCREEN_W, 3, 0x88);      /* the ground line */
    spr_show(0, cx - 14, 94, P_BUGGY0, 0);    /* a buggy on it */
    spr_show(1, cx + 2, 94, P_BUGGY0 + 1, 0);
    txt_center(124, "HIGH SCORES", C_WHITE, 1);
    for (i = 0; i < HS_ENTRIES; i++) {
        char line[16], *p = line;
        uint8_t k;
        *p++ = '1' + i;
        *p++ = ' ';
        for (k = 0; k < 3; k++) *p++ = hs_table[i].name[k];
        *p++ = ' ';
        *p++ = ' ';
        num_str(p, (long)hs_table[i].score);
        txt(SCREEN_W / 2 - 40, 136 + i * 10, line, i ? C_GRAY : C_BRYEL, 1);
    }
    txt(40, 194, "O/P SPEED   Q/SPACE JUMP", C_GREEN, 1);
    txt(40, 204, "A/FIRE SHOOT   KEMPSTON", C_GREEN, 1);
    view_title_blink(blink);
}

void view_title_blink(uint8_t on)
{
    clear_a(0, 220, SCREEN_W - 1, 234);
    if (on) txt_center(220, "PRESS FIRE TO START", C_BRYEL, 2);
}

/* the initials screen (the SDK's editor drives it) */
void view_initials(const char *name, uint8_t pos, long score)
{
    char buf[12];
    uint8_t i;
    if (!name) {                        /* the screen */
        view_hide_sprites();
        copper_still();
        ZXN_WRITE_REG(0x6B, 0x00);      /* tilemap off: plain black */
        l2w_fill(0, 0, SCREEN_W, SCREEN_H, TR);
        txt_center(60, "NEW HIGH SCORE", C_BRYEL, 2);
        num_str(buf, score);
        txt_center(90, buf, C_WHITE, 2);
        txt_center(130, "TYPE YOUR INITIALS", C_GREEN, 1);
        txt_center(142, "OR UP/DOWN AND FIRE", C_GREEN, 1);
        return;
    }
    l2w_fill(SCREEN_W / 2 - 30, 166, 60, 22, TR);
    for (i = 0; i < 3; i++) {
        char c[2] = {name[i], 0};
        uint16_t x = SCREEN_W / 2 - 26 + i * 18;
        if (i == pos) l2w_fill(x - 2, 166, 16, 20, 0x25);
        txt(x, 169, c, C_WHITE, 2);
    }
}
