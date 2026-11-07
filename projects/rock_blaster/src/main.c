/*
 * Rock Blaster for the ZX Spectrum 48K - port of amiga_mcp/examples/rock_blaster.
 *
 * game.c is the original game logic. This file is the Spectrum front end:
 * the original's vector shapes drawn as XOR wireframes (sdk line48/vec48)
 * straight onto the screen, which wraps around like the playfield; only
 * the shapes that moved are redrawn. Beeper sound (AY effects and the
 * original's MOD on a 128K), a title screen and a high-score table.
 *
 * Controls: O/P rotate, Q thrust, SPACE/M/0 fire; Kempston, Sinclair 1/2 and
 * cursor joysticks (up = thrust).
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include <z80.h>
#include "game.h"
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"
#include "line48.h"
#include "vec48.h"
#include "music.h"
#include "lowmem.h"

/* the bot in test.py reads the whole state by symbol (layout in test.py) */
GameState gs;

/* ------------------------------------------------------------ shapes */

/* the original's outlines (draw.c), scaled to the screen at start-up */
static const vec_pt ship_src[5] = {{10, 0}, {-7, -6}, {-4, -2}, {-4, 2}, {-7, 6}};
static const vec_pt flame_src[3] = {{-5, -3}, {-12, 0}, {-5, 3}};
static const vec_pt rock_src[3][4][8] = {
    {   /* large, radius ~20 */
        {{ -8,-18},{  8,-20},{ 20,-8},{ 18,  4},{ 12, 18},{ -4, 20},{-18, 10},{-20, -6}},
        {{-12,-16},{  4,-20},{ 18,-12},{ 20, 2},{ 10, 18},{ -8, 16},{-20,  4},{-16,-10}},
        {{ -6,-20},{ 10,-16},{ 20, -4},{ 16, 10},{  4, 20},{-10, 16},{-20,  2},{-14,-12}},
        {{-14,-14},{  2,-20},{ 16,-14},{ 20,  0},{ 14, 16},{ -2, 20},{-18,  8},{-18, -8}},
    },
    {   /* medium, ~12 */
        {{ -4,-10},{  4,-12},{ 12, -4},{ 10,  4},{  6, 10},{ -2, 12},{-10,  6},{-12, -2}},
        {{ -6, -8},{  2,-12},{ 10, -6},{ 12,  2},{  6, 10},{ -4, 10},{-12,  2},{ -8, -6}},
        {{ -2,-12},{  6,-10},{ 12, -2},{  8,  6},{  2, 12},{ -6,  8},{-12,  0},{ -8, -8}},
        {{ -8, -8},{  2,-12},{ 10, -8},{ 12,  0},{  8,  8},{  0, 12},{-10,  4},{-10, -4}},
    },
    {   /* small, ~6 */
        {{ -2,-5},{ 2,-6},{ 6,-2},{ 5, 2},{ 3, 5},{-1, 6},{-5, 3},{-6,-1}},
        {{ -3,-4},{ 1,-6},{ 5,-3},{ 6, 1},{ 3, 5},{-2, 5},{-6, 1},{-4,-3}},
        {{ -1,-6},{ 3,-5},{ 6,-1},{ 4, 3},{ 1, 6},{-3, 4},{-6, 0},{-4,-4}},
        {{ -4,-4},{ 1,-6},{ 5,-4},{ 6, 0},{ 4, 4},{ 0, 6},{-5, 2},{-5,-2}},
    },
};
/* a life in the HUD (the original's small triangle) */
static const vec_pt life_pts[3] = {{4, 0}, {-3, 3}, {-3, -3}};

#define SCALE 205                       /* 0.8: the 320 x 240 world on 256 x 192 */
#define SHIP_BITS 7                     /* 128 drawn angles (the ship turns in 256) */
#define FLAME_BITS 6

/* Shapes are compiled polygons (line48.h poly_compile: each edge's octant
   and length worked out once), built at start-up into the RAM below the
   program (lowmem.h). */
#define ship_poly  ((uint8_t *)LOW_SHIP_POLY)
#define flame_poly ((uint8_t *)LOW_FLAME_POLY)
#define rock_poly  ((uint8_t *)LOW_ROCK_POLY)
#define sx_tab     ((uint8_t *)LOW_SX_TAB)                     /* world pixel -> screen */
#define sy_tab     ((uint8_t *)LOW_SY_TAB)
#define pts_mem    ((uint8_t *)LOW_POINTS)
static const uint8_t *rock_polys[12];                          /* size * 4 + shape */

static void shapes_init(void)
{
    uint8_t s, k;
    uint16_t i;
    vec_pt tmp[8];
    for (s = 0; s < 3; s++)
        for (k = 0; k < 4; k++) {
            uint8_t *p = rock_poly + (s * 4 + k) * POLY_BYTES(8);
            vec_scale(rock_src[s][k], 8, SCALE, tmp);
            poly_compile(tmp, 8, p);
            rock_polys[s * 4 + k] = p;
        }
    vec_poly_table(ship_src, 5, SCALE, SHIP_BITS, ship_poly);
    vec_poly_table(flame_src, 3, SCALE, FLAME_BITS, flame_poly);
    for (i = 0; i < WORLD_W; i++) sx_tab[i] = (uint8_t)((i * 4) / 5);
    for (i = 0; i < WORLD_H; i++) sy_tab[i] = (uint8_t)((i * 4) / 5);
}

/* ------------------------------------------------------------ what is on screen */

/* Everything in the playfield is XOR, so drawing a shape a second time takes
   it off whatever else is there. After the logic, plan_frame() works out
   what should be shown; after the next HALT, draw_frame() takes off the
   shapes that changed and puts on the new ones (poly_sync). */
static poly_at rock_on[MAX_ROCKS], rock_want[MAX_ROCKS];
static poly_at ship_on[2], ship_want[2];                   /* ship, flame */

/* bullets (2x2) and particles as one point list, both buffers */
#define MAX_POINTS (MAX_PARTICLES + MAX_BULLETS * 4)
#define pts_buf(i) (pts_mem + (i) * (MAX_POINTS * 2))
static uint8_t pts_n[2], pts_cur;

/* objs.asm */
extern const void *pr_rocks, *pr_sx, *pr_sy, *pr_polys, *pp_bullets, *pp_parts;
extern poly_at *pr_want;
extern uint8_t *pp_out;
extern uint8_t pp_n, pp_parts_n;
extern void pr_core(void);
extern void pp_core(void);

static void plan_frame(void)
{
    pr_rocks = gs.rocks;
    pr_want = rock_want;
    pr_sx = sx_tab;
    pr_sy = sy_tab;
    pr_polys = rock_polys;
    pr_core();

    ship_want[0].poly = ship_want[1].poly = 0;
    if (gs.ship.alive && gs.state != STATE_GAMEOVER &&
        !(gs.ship.invuln_timer > 0 && (gs.frame & 4))) {      /* blink while invulnerable */
        ship_want[0].x = ship_want[1].x = sx_tab[gs.ship.px];
        ship_want[0].y = ship_want[1].y = sy_tab[gs.ship.py];
        ship_want[0].poly = vec_poly_at(ship_poly, 5, SHIP_BITS, gs.ship.angle);
        if (gs.ship.thrust_on && (gs.frame & 2))
            ship_want[1].poly = vec_poly_at(flame_poly, 3, FLAME_BITS, gs.ship.angle);
    }

    pp_out = pts_buf(pts_cur ^ 1);
    pp_bullets = gs.bullets;
    pp_parts = gs.particles;
    pp_parts_n = particles_live;
    pp_core();
    pts_n[pts_cur ^ 1] = pp_n;
}

static uint16_t draws;                  /* screen updates, for the fps log */

/* rock_want holds the even slots, then the odd ones (objs.asm); after a
   frame that overran, only one half is redrawn (they take turns), so a busy
   screen keeps a better frame rate for the ship, bullets and explosions */
static uint8_t rock_half;

static void draw_frame(uint8_t busy)
{
    line_mode = LINE_XOR;
    plot_list(pts_buf(pts_cur), pts_n[pts_cur]);              /* points off */
    if (busy) {
        rock_half ^= MAX_ROCKS / 2;
        poly_sync(rock_on + rock_half, rock_want + rock_half, MAX_ROCKS / 2);
    }
    else poly_sync(rock_on, rock_want, MAX_ROCKS);
    poly_sync(ship_on, ship_want, 2);
    pts_cur ^= 1;
    plot_list(pts_buf(pts_cur), pts_n[pts_cur]);              /* points on */
    draws++;
}

/* ------------------------------------------------------------ HUD (XOR too) */

static char hud_score[8], hud_level[4];
static int16_t hud_lives;

/* v as `digits` decimal digits (powers of ten subtracted: no 32-bit divide) */
static const uint32_t pow10[7] = {1000000, 100000, 10000, 1000, 100, 10, 1};

static void fmt_num(char *s, uint32_t v, uint8_t digits)
{
    const uint32_t *p = pow10 + 7 - digits;
    while (digits--) {
        char c = '0';
        while (v >= *p) { v -= *p; c++; }
        *s++ = c;
        p++;
    }
    *s = 0;
}

static void xor_lives(int16_t from, int16_t to)
{
    int16_t i;
    line_mode = LINE_XOR;
    for (i = from; i < to && i < 5; i++) line_poly(8 + i * 11, 14, life_pts, 3, 1);
}

/* change only the characters that changed: XOR the old one off, the new on */
static void hud_text(char *shown, const char *now, uint8_t col)
{
    char a[2], b[2];
    a[1] = b[1] = 0;
    for (; *now; shown++, now++, col++)
        if (*shown != *now) {
            a[0] = *shown;
            b[0] = *now;
            if (a[0]) text_xor(col, 1, a, 0);
            text_xor(col, 1, b, 0);
            *shown = *now;
        }
}

static uint32_t hud_score_v;
static int16_t hud_level_v;

static void hud(void)
{
    char s[8];
    if (hud_score_v != gs.score || !hud_score[0]) {
        hud_score_v = gs.score;
        fmt_num(s, gs.score, 7);
        hud_text(hud_score, s, 1);
    }
    if (hud_level_v != gs.level || !hud_level[0]) {
        hud_level_v = gs.level;
        s[0] = 'L';
        fmt_num(s + 1, gs.level, 2);
        hud_text(hud_level, s, 28);
    }
    if (hud_lives != gs.lives) {
        if (gs.lives < hud_lives) xor_lives(gs.lives, hud_lives);
        else xor_lives(hud_lives, gs.lives);
        hud_lives = gs.lives;
    }
}

#define A_PLAY (INK_WHITE | BRIGHT)
#define A_HUD  (INK_YELLOW | BRIGHT)
#define A_LIVES (INK_CYAN | BRIGHT)

static void play_screen(void)
{
    screen_clear(A_PLAY, INK_BLACK);
    attr_fill(0, 0, 32, 1, A_HUD);
    attr_fill(0, 1, 8, 2, A_LIVES);
    memset(rock_on, 0, sizeof(rock_on));
    memset(ship_on, 0, sizeof(ship_on));
    pts_n[0] = pts_n[1] = 0;
    memset(hud_score, 0, sizeof(hud_score));
    memset(hud_level, 0, sizeof(hud_level));
    hud_lives = 0;
    hud();
}

static void gameover_text(void)
{
    char s[16];
    attr_fill(0, 10, 32, 4, INK_RED | BRIGHT);
    text_xor(11, 80, "GAME OVER", 1);
    memcpy(s, "SCORE ", 6);
    fmt_num(s + 6, gs.score, 7);
    text_xor(10, 100, s, 0);
}

/* ------------------------------------------------------------ sound */

static uint8_t ev_shoot, ev_large, ev_small, ev_die;
void sfx_shoot(void) { ev_shoot = 1; }
void sfx_explode_large(void) { ev_large = 1; }
void sfx_explode_small(void) { ev_small = 1; }
void sfx_die(void) { ev_die = 1; }

static void sound_events(void)
{
    if (ay_present) {
        /* the original's effects: a falling blip, two noise bursts, a falling
           tone for the ship (music on the other two channels) */
        if (ev_die) ay_sfx(0, 200, 15, 1, 12);
        else if (ev_large) ay_sfx(0, 0, 15, 1, 0);
        else if (ev_small) ay_sfx(0, 0, 12, 2, 0);
        else if (ev_shoot) ay_sfx(0, 60, 13, 2, 10);
        sound_tick();
        rb_music_tick();
    } else {
        /* beeper: blocking, so every sound is a few thousand T-states */
        if (ev_die) { beep_sweep(40, 160, 8, 2); beep_noise(300); }
        else if (ev_large) beep_noise(220);
        else if (ev_small) beep_noise(110);
        else if (ev_shoot) beep_sweep(16, 40, 8, 1);
    }
    ev_shoot = ev_large = ev_small = ev_die = 0;
}

/* ------------------------------------------------------------ title */

static void title_screen(void)
{
    static const uint8_t deco[4][3] = {{24, 52, 0}, {230, 64, 2}, {22, 174, 1}, {234, 172, 3}};
    uint8_t i;
    screen_clear(INK_WHITE | BRIGHT, INK_BLACK);
    attr_fill(0, 1, 32, 2, INK_YELLOW | BRIGHT);
    text_big(4, 1, "ROCK");
    attr_fill(14, 1, 18, 2, INK_WHITE | BRIGHT);
    text_big(14, 1, "BLASTER");
    attr_fill(0, 4, 32, 1, INK_CYAN);
    text_center(4, "ZX SPECTRUM EDITION");
    line_mode = LINE_OR;
    for (i = 0; i < 4; i++)
        poly_draw(deco[i][0], deco[i][1], rock_polys[(i & 1) * 4 + deco[i][2]]);
    poly_draw(128, 118, vec_poly_at(ship_poly, 5, SHIP_BITS, 192));
    attr_fill(0, 6, 32, 1, INK_YELLOW | BRIGHT);
    text_center(6, "HIGH SCORES");
    hs_draw(10, 7);
    attr_fill(0, 13, 32, 1, INK_BLUE | BRIGHT);
    line_draw(40, 106, 215, 106);
    line_draw(40, 108, 215, 108);
    attr_fill(0, 16, 32, 4, INK_WHITE);
    zx_print_at(8, 16, "O / P   ROTATE");
    zx_print_at(8, 17, "Q       THRUST");
    zx_print_at(8, 18, "SPACE   FIRE");
    attr_fill(0, 19, 32, 1, INK_CYAN);
    text_center(19, "KEMPSTON / SINCLAIR OK");
}

/* ------------------------------------------------------------ main loop */

static void log_state(void)
{
    static uint8_t last_state = 0xFF;
    static int16_t last_lives = -1, last_level = -1;
    static uint32_t last_score = 0xFFFFFFFF;
    if (gs.state != last_state) {
        static const char *const names[4] = {"title", "playing", "dead", "gameover"};
        zx_log("rb: state=%s score=%lu lives=%d level=%d\n", names[gs.state], gs.score, gs.lives, gs.level);
        last_state = gs.state;
    }
    if (gs.score != last_score) { zx_log("rb: score=%lu\n", gs.score); last_score = gs.score; }
    if (gs.lives != last_lives) { zx_log("rb: lives=%d\n", gs.lives); last_lives = gs.lives; }
    if (gs.level != last_level) { zx_log("rb: level=%d\n", gs.level); last_level = gs.level; }
}

int main(void)
{
    uint8_t prev_state, planned = 0;
    uint16_t fps_frames = 0;

    gfx_init();
    line48_init();
    shapes_init();
    rb_music_init();
    input_init();
    input_fast();
    ay_init();
    hs_init(200);                  /* 1000 800 600 400 200 */
    rnd_seed(12345);
    gs.state = STATE_TITLE;
    frame_init();
    frame_hook = input_sample;     /* taps shorter than a slow frame still count */
    input_latch(1);
    title_screen();
    zx_log("rb: ready kempston=%u ay=%u\n", input_kempston, ay_present);

    for (;;) {
        uint8_t b, ticks, tick;
        prev_state = gs.state;

        /* fixed timestep: the logic runs once per 50 Hz frame that passed
           (up to 4), so the game keeps the original's speed when a busy
           screen takes longer to draw */
        ticks = frame_wait();
        if (ticks > 4) ticks = 4;
        if (gs.state != STATE_TITLE) {
            if (planned) {
                draw_frame(ticks > 1);
                hud();
            }
        }
        else if ((frame_count & 31) == 0) {
            attr_fill(0, 22, 32, 1, (frame_count & 32) ? (INK_YELLOW | BRIGHT) : INK_BLACK);
            text_center(22, "PRESS FIRE TO START");
        }
        sound_events();

        b = input_read();
        if (gs.state == STATE_TITLE) {
            if (input_pressed() & IN_FIRE) {
                rnd_seed(frame_count | 1);          /* a different game each time */
                game_init(&gs);
            }
        } else {
            for (tick = 0; tick < ticks; tick++) {
                game_update(&gs, (b & IN_LEFT) != 0, (b & IN_RIGHT) != 0,
                            (b & IN_UP) != 0, (b & IN_FIRE) != 0);
                log_state();
                if (gs.state != prev_state) break;
            }
            fps_frames += ticks;
            if (fps_frames >= 250) {
                zx_log("rb: fps=%u rocks=%d overruns=%u\n", draws / 5, gs.rock_count, frame_overruns);
                if (ay_present) {
                    uint8_t mix, vb, vc;
                    z80_outp(0xFFFD, 7); mix = z80_inp(0xFFFD);
                    z80_outp(0xFFFD, 9); vb = z80_inp(0xFFFD);
                    z80_outp(0xFFFD, 10); vc = z80_inp(0xFFFD);
                    zx_log("rb: ay mix=%u volb=%u volc=%u\n", mix, vb, vc);
                }
                fps_frames -= 250;
                draws = 0;
            }
        }
        planned = 0;

        if (gs.state != prev_state) {
            if (prev_state == STATE_TITLE) {
                log_state();
                play_screen();
                if (ay_present) rb_music_start();
                fps_frames = draws = 0;
            }
            else if (gs.state == STATE_GAMEOVER) {
                gameover_text();
                zx_log("rb: gameover score=%lu overruns=%u\n", gs.score, frame_overruns);
            }
            else if (gs.state == STATE_TITLE) {
                int8_t place = hs_rank(gs.score);
                if (ay_present) { rb_music_stop(); ay_init(); }
                if (place >= 0) hs_enter(place, gs.score, "rb");
                title_screen();
                zx_log("rb: title\n");
            }
        }
        if (gs.state != STATE_TITLE) {
            plan_frame();
            planned = 1;
        }
    }
}
