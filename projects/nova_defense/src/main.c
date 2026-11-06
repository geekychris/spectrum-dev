/*
 * Nova Defense for the ZX Spectrum 48K - port of amiga_mcp/examples/nova_defense.
 *
 * game.c is the original game logic (rules, scoring, timing). This file is
 * the Spectrum front end: 1-bit art with attribute colour per character row,
 * the swarm drawn as row strips (two rows a frame, rippling like the arcade
 * machine), XOR sprites for everything that moves, beeper sound (AY on a
 * 128K), a title screen and a high-score table.
 *
 * Controls: O/P or Q... see zxgame.h (keys, Sinclair, Kempston); fire = SPACE,
 * M, 0 or joystick fire.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "game.h"
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"

static GameState gs;
static InputState in;

/* for test bots (projects/nova_defense/test.py reads it by symbol): where the
   player and the alien bullets are; written once per frame, never read */
struct { int16_t player_x; int16_t bullet_x[3]; int16_t bullet_y[3]; uint8_t state; } nova_probe;

static void update_probe(void)
{
    uint8_t i;
    nova_probe.player_x = gs.player_x;
    for (i = 0; i < 3; i++) {
        nova_probe.bullet_x[i] = gs.alien_bullets[i].active ? gs.alien_bullets[i].x : -1;
        nova_probe.bullet_y[i] = gs.alien_bullets[i].y;
    }
    nova_probe.state = gs.state;
}

/* ------------------------------------------------------------ art */

/* 14x7 aliens on an 8-line cell, two frames each ('#' = ink; holes are the eyes) */
static const char *const art_alien[3][2][8] = {
    {   /* type C: eye pod, 30 pts */
        {"....######....", "..##########..", ".###..##..###.", "##############",
         "..##########..", "..#..#..#..#..", ".#..#....#..#.", ".............."},
        {"....######....", "..##########..", ".####..##.###.", "##############",
         "..##########..", "...#.#..#.#...", "...#..##..#...", ".............."},
    },
    {   /* type B: ray, 20 pts */
        {"#............#", "##....##....##", "###..####..###", "##############",
         ".####.##.####.", "....##..##....", "...##....##...", ".............."},
        {"......##......", "#....####....#", "##############", ".####.##.####.",
         "..##########..", "....##..##....", "....#....#....", ".............."},
    },
    {   /* type A: spore crawler, 10 pts */
        {"......##......", ".....####.....", "....##..##....", "...########...",
         "..##.####.##..", ".##..#..#..##.", "##..##..##..##", ".............."},
        {"......##......", ".....####.....", "....##..##....", "...########...",
         "..##.####.##..", "..#..#..#..#..", "...#.#..#.#...", ".............."},
    },
};
static const char *const art_player[8] = {
    ".......#.......", "......###......", "......#.#......", "..#..#####..#..",
    "..#.#######.#..", ".#############.", "###############", "##.##.....##.##",
};
static const char *const art_ufo[7] = {
    "........##......", "......######....", "####..#######...", "..##############",
    "####..#######...", "......######....", "........##......",
};
static const char *const art_boom[8] = {
    "#..#..#.", ".#.#.#..", "..###...", "###.####", "..###...", ".#.#.#..", "#..#..#.", "........",
};

static uint8_t pre_alien[3][2][8 * 3 * 8];   /* 2 bytes wide -> 3 per pre-shifted line */
static const uint8_t *alien_shift[3][2][8];   /* each shift's start in pre_alien */
static uint8_t pre_player[8 * 3 * 8];
static uint8_t pre_ufo[8 * 3 * 7];
static uint8_t pre_boom[8 * 2 * 8];

/* (written with a walking pointer + mask: zsdcc miscompiled the
   out[y * bw + (x >> 3)] |= 0x80 >> (x & 7) form) */
static void art_to_bits(const char *const *art, uint8_t w, uint8_t h, uint8_t *out)
{
    uint8_t bw = (w + 7) / 8, y, x;
    memset(out, 0, bw * h);
    for (y = 0; y < h; y++) {
        const char *s = art[y];
        uint8_t *o = out + y * bw, m = 0x80;
        for (x = 0; x < w; x++) {
            if (s[x] == '#') *o |= m;
            m >>= 1;
            if (!m) { m = 0x80; o++; }
        }
    }
}

static void art_init(void)
{
    uint8_t bits[16], t, f;
    for (t = 0; t < 3; t++)
        for (f = 0; f < 2; f++) {
            uint8_t k;
            art_to_bits(art_alien[t][f], 14, 8, bits);
            spr_preshift(bits, 2, 8, pre_alien[t][f]);
            for (k = 0; k < 8; k++) alien_shift[t][f][k] = pre_alien[t][f] + k * 24;
        }
    art_to_bits(art_player, 15, 8, bits);
    spr_preshift(bits, 2, 8, pre_player);
    art_to_bits(art_ufo, 16, 7, bits);
    spr_preshift(bits, 2, 7, pre_ufo);
    art_to_bits(art_boom, 8, 8, bits);
    spr_preshift(bits, 1, 8, pre_boom);
}

/* ------------------------------------------------------------ colours */

#define A_HUD     (INK_WHITE | BRIGHT)
#define A_UFO     (INK_RED | BRIGHT)
#define A_GAP     (INK_WHITE)
#define A_SHIELD  (INK_GREEN | BRIGHT)
#define A_PLAYER  (INK_CYAN | BRIGHT)
#define A_GROUND  (INK_YELLOW)
static const uint8_t alien_attr[3] = {INK_MAGENTA | BRIGHT, INK_GREEN | BRIGHT, INK_CYAN | BRIGHT};

#define ROW_UFO 1
#define ROW_PLAY_TOP 2
#define ROW_SHIELD (SHIELD_Y / 8)
#define ROW_PLAYER (PLAYER_Y / 8)
#define GROUND_Y 178

/* colour every play row by what is in it (after a drop): one memset per row */
static void colour_rows(void)
{
    uint8_t row, top = gs.swarm.grid_y >> 3;
    uint8_t *a = (uint8_t *)0x5800 + ROW_PLAY_TOP * 32;
    for (row = ROW_PLAY_TOP; row < ROW_PLAYER; row++, a += 32) {
        uint8_t v = (row == ROW_SHIELD || row == ROW_SHIELD + 1) ? A_SHIELD : A_GAP;
        if (row >= top && row < top + ALIEN_ROWS) v = alien_attr[(row - top) >> 1];
        memset(a, v, 32);
    }
}

/* rows (8-line bands, bit per character row 0..23) redrawn this frame: sprites
   over them are taken off first */
static uint8_t redraw_row[24];

static LONG hud_score = -1, hud_hi = -1;       /* what the HUD shows */
static WORD hud_lives = -1, hud_wave = -1;

/* ------------------------------------------------------------ swarm rows */

/* what each alien row looks like on screen now: redrawn when it changes */
typedef struct { WORD x, y; UBYTE anim, alive; } row_sig;
static row_sig drawn[ALIEN_ROWS];
static uint8_t next_row;
static WORD coloured_y = -1;

static uint8_t alive_mask[ALIEN_ROWS];         /* refreshed once per frame */

static WORD alive_seen = -1;

static void compute_alive(void)
{
    if (alive_seen == gs.swarm.alive_count) return;   /* only changes when one dies */
    alive_seen = gs.swarm.alive_count;
    {
    uint8_t r, m;
    const BOOL *a = &gs.swarm.alive[0][0];
    for (r = 0; r < ALIEN_ROWS; r++, a += ALIEN_COLS) {
        m = 0;                                    /* bit c = column c alive */
        if (a[0]) m |= 0x01;
        if (a[1]) m |= 0x02;
        if (a[2]) m |= 0x04;
        if (a[3]) m |= 0x08;
        if (a[4]) m |= 0x10;
        if (a[5]) m |= 0x20;
        if (a[6]) m |= 0x40;
        if (a[7]) m |= 0x80;
        alive_mask[r] = m;
    }
    }
}

#define row_alive(r) alive_mask[r]

/* swarm_row.asm */
extern uint8_t *row_dest;
extern uint8_t row_c0, row_wc, row_mask;
extern int16_t row_x;
extern const WORD *row_offs;
extern const uint8_t *const *row_shift;
extern void swarm_row_draw(void);

/* clear + draw alien row r where it is now (and wipe where it was, if it moved down) */
static void draw_row(uint8_t r)
{
    WORD y = ALIEN_Y(&gs, r), left;
    /* It dropped. Rows are one drop apart, so its old place now belongs to
       the row above, except above the top row: wipe only that. */
    if (drawn[r].y >= 0 && drawn[r].y != y && drawn[r].y < ALIEN_Y(&gs, 0)) {
        row_dest = zx_row[drawn[r].y];
        row_c0 = 0;
        row_wc = 32;
        row_offs = 0;                                   /* clear only */
        swarm_row_draw();
    }
    drawn[r].x = gs.swarm.grid_x;
    drawn[r].y = y;
    drawn[r].anim = gs.swarm.anim_frame;
    drawn[r].alive = alive_mask[r];
    if (y >= 192) return;
    (void)left;
    row_wc = 0;                                         /* the row rewrites itself */
    row_dest = zx_row[y];
    row_mask = alive_mask[r];
    row_x = gs.swarm.grid_x;
    row_offs = alien_col_off[r & 1];
    row_shift = alien_shift[r / 2][gs.swarm.anim_frame];
    swarm_row_draw();
}

static uint8_t row_changed(uint8_t r)
{
    return drawn[r].x != gs.swarm.grid_x || drawn[r].y != ALIEN_Y(&gs, r) ||
           drawn[r].anim != gs.swarm.anim_frame || drawn[r].alive != row_alive(r);
}

/* plan: up to `budget` changed rows this frame, round robin -> redraw_rows */
static uint8_t plan[ALIEN_ROWS], n_plan;

static void plan_swarm(uint8_t budget)
{
    uint8_t i;
    compute_alive();
    n_plan = 0;
    memset(redraw_row, 0, sizeof(redraw_row));
    for (i = 0; i < ALIEN_ROWS && budget; i++) {
        uint8_t r = (next_row + i) % ALIEN_ROWS;
        if (row_changed(r)) {
            WORD y = ALIEN_Y(&gs, r);
            plan[n_plan++] = r;
            if (y < 192) redraw_row[(uint8_t)y >> 3] = 1;
            if (drawn[r].y >= 0 && drawn[r].y < 192) redraw_row[(uint8_t)drawn[r].y >> 3] = 1;
            budget--;
            next_row = r + 1;
        }
    }
    /* the HUD rewrites row 0 when the score changes */
    if (hud_score != gs.score || hud_hi != gs.hiscore) redraw_row[0] = 1;
    /* shield rows being redrawn also wipe sprites over them */
    if (shield_dirty[0] | shield_dirty[1] | shield_dirty[2])
        redraw_row[SHIELD_Y >> 3] = redraw_row[(SHIELD_Y >> 3) + 1] = 1;
}

static void draw_swarm(void)
{
    uint8_t i;
    if (coloured_y != gs.swarm.grid_y) {
        coloured_y = gs.swarm.grid_y;
        colour_rows();
    }
    for (i = 0; i < n_plan; i++) draw_row(plan[i]);
}

static void reset_swarm_view(void)
{
    uint8_t r;
    alive_seen = -1;
    for (r = 0; r < ALIEN_ROWS; r++) {
        drawn[r].y = -1;
        drawn[r].alive = 0xFF;
    }
    coloured_y = -1;
    next_row = 0;
}

/* ------------------------------------------------------------ shields */

/* redraw the pixel rows of shield s marked in `rows` */
static void draw_shield_rows(uint8_t s, uint16_t rows)
{
    Shield *sh = &gs.shields[s];
    uint8_t y, x, col = sh->x >> 3, first = sh->x & 7;
    for (y = 0; y < SHIELD_H; y++) {
        uint8_t line[4], *o, m;
        const UBYTE *p;
        if (!(rows & (1 << y))) continue;
        line[0] = line[1] = line[2] = line[3] = 0;
        o = line;
        m = gfx_bit[first];
        p = sh->pixels[y];
        for (x = 0; x < SHIELD_W; x++) {
            if (p[x]) *o |= m;
            m >>= 1;
            if (!m) { m = 0x80; o++; }
        }
        memcpy(zx_row[sh->y + y] + col, line, 4);
    }
}

static void draw_shields(void)
{
    uint8_t s;
    for (s = 0; s < SHIELD_COUNT; s++)
        if (shield_dirty[s]) {
            draw_shield_rows(s, shield_dirty[s]);
            shield_dirty[s] = 0;
        }
}

/* ------------------------------------------------------------ moving sprites (XOR) */

/* Each frame: work out the sprites that should be on screen; any shown one
   that is no longer wanted, has moved, or lies on a swarm row about to be
   redrawn is XORed off, then the missing ones are XORed on. A sprite that
   stays put costs nothing. */
typedef struct { int16_t x, y; uint8_t kind, h; } xspr;
#define MAX_X 16
static xspr shown[MAX_X], want[MAX_X];
static uint8_t n_shown, n_want;

static void xor_bullet(int16_t x, int16_t y, uint8_t len, uint8_t zig)
{
    uint8_t i;
    for (i = 0; i < len; i++, y++) {
        uint8_t xx = x + (zig ? (i & 1) : 0);
        if (y >= 0 && y < 192) zx_row[y][xx >> 3] ^= gfx_bit[xx & 7];
    }
}

enum { K_PLAYER, K_PBULLET, K_ABULLET, K_UFO, K_BOOM };
static const uint8_t kind_h[5] = {8, 4, 3, 7, 8};
static const int8_t kind_dy[5] = {0, 0, 0, 0, -4};

static void xor_one(const xspr *s)
{
    switch (s->kind) {
    case K_PLAYER: spr_xor(s->x, s->y, pre_player, 2, 8); break;
    case K_PBULLET: xor_bullet(s->x, s->y, 4, 0); break;
    case K_ABULLET: xor_bullet(s->x, s->y, 3, 1); break;
    case K_UFO: if (s->x >= 0 && s->x <= 240) spr_xor(s->x, s->y, pre_ufo, 2, 7); break;
    case K_BOOM: if (s->x >= 4 && s->x < 248 && s->y >= 4) spr_xor(s->x - 4, s->y - 4, pre_boom, 1, 8); break;
    }
}

static void want_add(uint8_t kind, int16_t x, int16_t y)
{
    if (n_want < MAX_X) {
        want[n_want].kind = kind;
        want[n_want].x = x;
        want[n_want].y = y;
        want[n_want].h = kind_h[kind];
        n_want++;
    }
}

static void collect_wanted(void)
{
    uint8_t i;
    n_want = 0;
    if (gs.state == STATE_PLAYING || gs.state == STATE_WAVE_CLEAR ||
        (gs.state == STATE_DYING && (gs.state_timer & 4)))
        want_add(K_PLAYER, gs.player_x, PLAYER_Y);
    if (gs.player_bullet.active) want_add(K_PBULLET, gs.player_bullet.x, gs.player_bullet.y);
    for (i = 0; i < MAX_ALIEN_BULLETS; i++)
        if (gs.alien_bullets[i].active) want_add(K_ABULLET, gs.alien_bullets[i].x, gs.alien_bullets[i].y);
    if (gs.ufo.active) want_add(K_UFO, gs.ufo.x, UFO_Y);
    for (i = 0; i < MAX_EXPLOSIONS; i++)
        if (gs.explosions[i].active) want_add(K_BOOM, gs.explosions[i].x, gs.explosions[i].y);
}


static uint8_t on_redraw_row(const xspr *s)
{
    int16_t y0 = s->y + kind_dy[s->kind], y1 = y0 + s->h - 1;
    if (y1 < 0 || y0 > 191) return 0;
    if (y0 < 0) y0 = 0;
    if (y1 > 191) y1 = 191;
    return redraw_row[(uint8_t)y0 >> 3] | redraw_row[(uint8_t)y1 >> 3];
}

/* prepare (after the logic): which shown sprites go, which wanted ones are new */
static uint8_t going[MAX_X];

static void sprites_decide(void)
{
    uint8_t i, j;
    collect_wanted();
    for (i = 0; i < n_shown; i++) {
        going[i] = 1;
        if (!on_redraw_row(&shown[i]))
            for (j = 0; j < n_want; j++)
                if (want[j].kind == shown[i].kind && want[j].x == shown[i].x && want[j].y == shown[i].y) {
                    want[j].kind = 0xFF;                        /* already there */
                    going[i] = 0;
                    break;
                }
    }
}

/* after the HALT, before the swarm/shields are redrawn: take off what goes */
static void sprites_erase(void)
{
    uint8_t i, k = 0;
    for (i = 0; i < n_shown; i++) {
        if (going[i]) xor_one(&shown[i]);
        else shown[k++] = shown[i];
    }
    n_shown = k;
}

/* after: put on what is missing */
static void sprites_draw(void)
{
    uint8_t j;
    for (j = 0; j < n_want && n_shown < MAX_X; j++)
        if (want[j].kind != 0xFF) {
            xor_one(&want[j]);
            shown[n_shown++] = want[j];
        }
}

/* ------------------------------------------------------------ HUD */


static void hud(void)
{
    if (hud_score != gs.score) { text_num(6, 0, gs.score, 6); hud_score = gs.score; }
    if (hud_hi != gs.hiscore) { text_num(26, 0, gs.hiscore, 6); hud_hi = gs.hiscore; }
    if (hud_lives != gs.lives || hud_wave != gs.wave) {
        uint8_t i;
        gfx_clear_rect(0, 184, 32, 8);
        for (i = 1; i < gs.lives && i < 6; i++) spr_or(i * 18 - 16, 184, pre_player, 2, 8);
        zx_print_at(24, 23, "WAVE");
        text_num(29, 23, gs.wave, 2);
        hud_lives = gs.lives;
        hud_wave = gs.wave;
    }
}

static void play_screen(void)
{
    uint8_t s;
    screen_clear(A_GAP, INK_BLACK);
    attr_fill(0, 0, 32, 1, A_HUD);
    attr_fill(0, ROW_UFO, 32, 1, A_UFO);
    attr_fill(0, ROW_PLAYER, 32, 1, A_PLAYER);
    attr_fill(0, 22, 32, 1, A_GROUND);
    attr_fill(0, 23, 32, 1, A_PLAYER);
    zx_print_at(0, 0, "SCORE");
    zx_print_at(23, 0, "HI");
    gfx_fill_rect(0, GROUND_Y, 32, 1, 0xFF);
    hud_score = hud_hi = -1;
    hud_lives = hud_wave = -1;
    hud();
    reset_swarm_view();
    for (s = 0; s < SHIELD_COUNT; s++) shield_dirty[s] = 0x0FFF;
    draw_shields();
    n_shown = 0;
}

/* ------------------------------------------------------------ sound */

static const uint16_t march_beep[3] = {115, 100, 88};     /* one short cycle each */
static const uint16_t march_ay[3] = {640, 538, 452};      /* the original's Paula periods ~ AY */
static const ay_note melody[] = {
    {504, 12}, {423, 12}, {336, 12}, {423, 12}, {504, 12}, {566, 12}, {504, 12}, {423, 12},
    {377, 12}, {336, 12}, {283, 12}, {336, 12}, {377, 12}, {423, 12}, {504, 12}, {566, 12}, {0, 0},
};

static void sound_events(void)
{
    if (ay_present) {
        if (gs.ev_shoot) ay_sfx(0, 120, 14, 2, 12);
        if (gs.ev_alien_hit) ay_sfx(1, 0, 14, 2, 0);
        if (gs.ev_ufo_hit) ay_sfx(1, 80, 15, 1, 6);
        if (gs.ev_player_hit) ay_sfx(1, 0, 15, 1, 0);
        if (gs.ev_march) ay_sfx(2, march_ay[gs.march_note] * 2, 12, 3, 0);
        else if (gs.ufo.active && (frame_count & 7) == 0) ay_sfx(0, (frame_count & 8) ? 60 : 80, 8, 2, 0);
        sound_tick();
        return;
    }
    /* beeper: blocking, so every sound is a few thousand T-states at most */
    if (gs.ev_player_hit) beep_noise(400);
    else if (gs.ev_ufo_hit) beep_sweep(20, 60, 10, 2);
    else if (gs.ev_alien_hit) beep_noise(120);
    else if (gs.ev_shoot) beep_sweep(20, 50, 10, 1);
    else if (gs.ev_march) beep(march_beep[gs.march_note], 1);
    else if (gs.ufo.active && (frame_count & 7) == 0) beep((frame_count & 8) ? 30 : 40, 3);
    if (gs.state == STATE_DYING && gs.state_timer > 40 && (gs.state_timer & 3) == 0) beep_noise(150);
}

/* ------------------------------------------------------------ title + high scores */

static void title_screen(void)
{
    static const char *const pts[3] = {"= 30 POINTS", "= 20 POINTS", "= 10 POINTS"};
    uint8_t t;
    screen_clear(INK_WHITE, INK_BLACK);
    attr_fill(0, 1, 32, 2, INK_YELLOW | BRIGHT);
    text_big(4, 1, "NOVA DEFENSE");
    attr_fill(0, 4, 32, 1, INK_CYAN);
    text_center(4, "ZX SPECTRUM EDITION");
    for (t = 0; t < 3; t++) {                    /* score legend, rows 6 8 10 */
        spr_or(84, 48 + t * 16, pre_alien[t][0], 2, 8);
        attr_fill(10, 6 + t * 2, 3, 1, alien_attr[t]);
        attr_fill(14, 6 + t * 2, 12, 1, INK_WHITE | BRIGHT);
        zx_print_at(14, 6 + t * 2, pts[t]);
    }
    spr_or(83, 97, pre_ufo, 2, 7);
    attr_fill(10, 12, 3, 1, A_UFO);
    attr_fill(14, 12, 12, 1, INK_WHITE | BRIGHT);
    zx_print_at(14, 12, "= MYSTERY");
    attr_fill(0, 14, 32, 1, INK_YELLOW | BRIGHT);
    text_center(14, "HIGH SCORES");
    attr_fill(0, 15, 32, 5, INK_WHITE | BRIGHT);
    hs_draw(10, 15);
    attr_fill(0, 23, 32, 1, INK_GREEN | BRIGHT);
    text_center(23, "QAOP/KEMPSTON/SINCLAIR  FIRE");
}

/* ------------------------------------------------------------ main loop */

static void log_state(void)
{
    static WORD last_state = -1, last_lives = -1, last_wave = -1;
    static LONG last_score = -1;
    if (gs.state != last_state) {
        static const char *names[5] = {"title", "playing", "dying", "gameover", "wave_clear"};
        zx_log("nova: state=%s score=%lu lives=%d wave=%d\n", names[gs.state], gs.score, gs.lives, gs.wave);
        last_state = gs.state;
    }
    if (gs.score != last_score) { zx_log("nova: score=%lu\n", gs.score); last_score = gs.score; }
    if (gs.lives != last_lives) { zx_log("nova: lives=%d\n", gs.lives); last_lives = gs.lives; }
    if (gs.wave != last_wave) { zx_log("nova: wave=%d\n", gs.wave); last_wave = gs.wave; }
}

int main(void)
{
    WORD prev_state;
    uint8_t planned = 0;
    gfx_init();
    art_init();
    input_init();
    ay_init();
    hs_init(100);                 /* table 500 400 300 200 100 */
    game_srand(12345);
    game_init(&gs);
    frame_init();
    title_screen();
    zx_log("nova: ready kempston=%u ay=%u\n", input_kempston, ay_present);

    for (;;) {
        uint8_t b, ticks, tick;
        prev_state = gs.state;

        /* fixed timestep: the logic runs once per 50 Hz frame that passed (up
           to 3), so the game keeps the original's speed even when a busy frame
           takes longer to draw */
        ticks = frame_wait();
        if (ticks > 3) ticks = 3;
        if (gs.state != STATE_TITLE && planned) {
            /* screen work first, while the beam is still in the top border */
            sprites_erase();
            draw_swarm();
            draw_shields();
            sprites_draw();
            hud();
        }
        else if ((frame_count & 31) == 0) {
            attr_fill(0, 21, 32, 1, (frame_count & 32) ? (INK_WHITE | BRIGHT) : INK_BLACK);
            text_center(21, "PRESS FIRE TO START");
        }
        sound_events();

        b = input_read();
        in.left = (b & IN_LEFT) != 0;
        in.right = (b & IN_RIGHT) != 0;
        in.fire = (b & IN_FIRE) != 0;
        in.fire_pressed = (input_pressed() & IN_FIRE) != 0;
        in.mouse_dx = 0;
        for (tick = 0; tick < ticks; tick++) {
            game_update(&gs, &in);
            in.fire_pressed = 0;                  /* an edge counts once */
            log_state();
            if (gs.state != prev_state) break;    /* screen changes happen below */
        }
        planned = 0;

        if (gs.state != prev_state) {
            if (prev_state == STATE_TITLE && gs.state == STATE_PLAYING) {
                play_screen();
                if (ay_present) music_play(2, melody, 7);
            }
            else if (gs.state == STATE_PLAYING && prev_state == STATE_WAVE_CLEAR) {
                play_screen();
            }
            else if (gs.state == STATE_GAMEOVER) {
                attr_fill(0, 11, 32, 2, INK_RED | BRIGHT);
                text_big(7, 11, "GAME OVER");
                if (ay_present) music_stop();
                zx_log("nova: gameover score=%lu overruns=%u\n", gs.score, frame_overruns);
            }
            else if (gs.state == STATE_TITLE) {
                int8_t place = hs_rank(gs.score);
                if (place >= 0) hs_enter(place, gs.score, "nova");
                title_screen();
                zx_log("nova: title\n");
            }
        }
        update_probe();
        /* prepare the next frame's screen work */
        if (gs.state != STATE_TITLE) {
            plan_swarm(2);
            sprites_decide();
            planned = 1;
        }
    }
}
