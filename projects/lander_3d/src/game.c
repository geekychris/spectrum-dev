/*
 * game.c - Lander 3D's game shell for the Next (the original's game.c):
 * input, the title (the autopilot flying a demo), flying (four 1/100 s
 * steps of the rules a 25 Hz tick), the outcome of each landing, ships and
 * levels, sound, and here the high scores.
 *
 * The rules run at the original's 25 ticks a second whatever the drawing
 * costs: each pass of the main loop runs a tick for every two frames that
 * went by (fixed timestep, at most 10 a pass; the original's 4 would slow
 * the game below 6 frames a second), then the picture catches up.
 *
 * port: keys. The original's W A S D (tip) and its cursor keys, a
 * Kempston, cursor or Sinclair 2 joystick; SPACE / J / fire full thrust,
 * Q / E the steady throttle, P pause, H help, ENTER start, BREAK (the
 * original's Esc) back to the title; B the autopilot (the original's
 * devbench hook "autopilot").
 * port: the autopilot flying the demo (and B) decides every 32 steps, not
 * every step: it lands all 30 levels either way, with nearly the same
 * scores (tools/ap_rate.c), at a tenth of a frame instead of more than one.
 * The game's own self-test (T held at start-up) flies it every step.
 * port: the high-score table and the initials (the original keeps a best
 * score only).
 */
#include <string.h>
#include <stdio.h>
#include <input.h>
#include <intrinsic.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "input2.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "screens.h"
#include "sound.h"
#include "pens.h"
#include "game_rules.h"
#include "prof.h"

Lander lander;
Level level;
uint8_t hud_dirty = 1;
hs_editor ed;
int8_t ed_place;

static uint8_t acc, demo_lv;
static const char *const state_names[] = { "TITLE", "PLAY", "RESULT", "OVER", "ENTRY" };

/* ---------------------------------------------------------------- input */
#define SRC (IN2_WASD | IN2_CURSOR | IN2_SINCLAIR2 | IN2_KEMPSTON1 | IN2_SPACE | IN2_ENTER)
#define K_THR_DN 1                       /* Q */
#define K_THR_UP 2                       /* E */
#define K_PAUSE  4                       /* P */
#define K_HELP   8                       /* H */
#define K_AUTO   16                      /* B */
#define K_ESC    32                      /* BREAK */

static volatile uint8_t isr_held, isr_pressed, isr_kheld, isr_kpressed;
static uint8_t held, pressed, kpressed;

/* the frame interrupt: the keys and joysticks, presses latched (a tap
   shorter than a pass of the loop still counts) */
#ifdef LD_PROF
uint16_t prof_sum[PR_N];
static uint16_t prof_line0, prof_lastl, prof_frames, prof_lastf;
static uint8_t prof_cur;
static uint16_t raster(void) { return (uint16_t)ZXN_READ_REG(0x1E) << 8 | ZXN_READ_REG(0x1F); }
void prof_mark(uint8_t id) __z88dk_fastcall
{
    static uint16_t l, f, d;
    intrinsic_di();
    l = raster();
    f = prof_frames;
    intrinsic_ei();
    if (l < prof_line0) l += 312;
    l -= prof_line0;
    d = l - prof_lastl;
    prof_lastl = l;
    l = f - prof_lastf;
    prof_lastf = f;
    while (l--) d += 312;
    prof_sum[prof_cur] += d;
    prof_cur = id;
}
#endif

void game_sample(void)
{
    uint8_t b, k = 0;
#ifdef LD_PROF
    prof_line0 = raster();
    prof_frames++;
#endif
    if (lander.state == LS_ENTRY) input_sample();   /* the initials editor's letters, latched */
    in2_scan();
    b = in2_bits(SRC);
    if (in2_key(6, 0x08)) b |= IN_FIRE;                  /* J */
    if (in2_key(2, 0x01)) k |= K_THR_DN;
    if (in2_key(2, 0x04)) k |= K_THR_UP;
    if (in2_key(5, 0x01)) k |= K_PAUSE;
    if (in2_key(6, 0x10)) k |= K_HELP;
    if (in2_key(7, 0x10)) k |= K_AUTO;
    if (in2_key(0, 0x01) && in2_key(7, 0x01)) { k |= K_ESC; b &= ~IN_FIRE; }   /* CAPS + SPACE */
    /* port: through locals: zsdcc compiled `isr_pressed |= b & ~isr_held`
       as `isr_pressed |= 0` (the volatile operands) */
    {
        uint8_t prev = isr_held, kprev = isr_kheld, p, q;
        p = b & (uint8_t)~prev;
        q = k & (uint8_t)~kprev;
        p |= isr_pressed;
        q |= isr_kpressed;
        isr_pressed = p;
        isr_kpressed = q;
        isr_held = b;
        isr_kheld = k;
    }
}

static void read_input(void)
{
    intrinsic_di();
    held = isr_held;
    pressed = isr_pressed;
    kpressed = isr_kpressed;
    isr_pressed = isr_kpressed = 0;
    intrinsic_ei();
}

/* ---------------------------------------------------------------- the flow */
void set_state(uint8_t s)
{
    lander.state = s;
    hud_dirty = 1;
    zx_log("ld: state %s score=%ld ships=%d level=%d\n", state_names[s], lander.score, (int)lander.ships, lander.level);
}

static uint8_t world_for(uint8_t menu, int16_t lv) { return menu < W_COUNT ? menu : (uint8_t)((lv - 1) % W_COUNT); }

static void palette(uint8_t w);

static void new_level(uint8_t w, int16_t lv)
{
    LOGIC();
    ld_new_level(&level, w, lv);
    ter_done();
    palette(w);
    rules_autopilot_reset();
}

static void begin_level(void)
{
    uint8_t w = world_for(lander.menu, lander.level);
    new_level(w, lander.level);
    hv.lim_vs = ld_worlds[w].lim_vs;             /* (for game_out.c, in another bank) */
    lander.steady = 0;
    lander.topped = level.fuel_max > level.params.fuel;
    hud_dirty = 1;
    zx_log("ld: level %s %d seed %lu fuel %ld%s\n", ld_worlds[w].name, lander.level, level.seed, FINT(level.fuel),
           lander.topped ? " (topped up)" : "");
}

static void start_game(uint8_t menu, int16_t lv)
{
    lander.menu = menu;
    lander.level = lv < 1 ? 1 : lv;
    lander.ships = 3;
    lander.score = 0;
    lander.demo = 0;
    lander.paused = lander.help = 0;
    snd_music(0);
    set_state(LS_PLAY);
    zx_log("ld: start menu=%u\n", menu);
    begin_level();
}

static void to_title(void)
{
    lander.demo = 1;
    lander.title_page = 0;
    lander.state_timer = 0;
    demo_lv = 0;
    new_level(W_MOON, 2);
    snd_music(1);
    set_state(LS_TITLE);
}

static void after_result(void)
{
    if (level.outcome == OUT_LANDED || level.outcome == OUT_OFFPAD) lander.level++;
    set_state(LS_PLAY);
    begin_level();
}

/* ---------------------------------------------------------------- a tick */
static void tick(void)
{
    if (lander.help || lander.paused || lander.state == LS_ENTRY) return;
    lander.frame++;
    if (lander.state_timer > 0) lander.state_timer--;
    if (lander.state == LS_RESULT && lander.state_timer == 0) { after_result(); return; }
    if (lander.state != LS_PLAY && lander.state != LS_TITLE) return;
    LOGIC();
    switch (rules_tick(held)) {
    case 1:                                      /* the demo goes round again */
        demo_lv = (uint8_t)((demo_lv + 1) % W_COUNT);
        new_level(demo_lv, 2);
        break;
    case 2:
        HUD();
        game_outcome();
        break;
    }
    if (lander.state == LS_TITLE && ++lander.state_timer >= 200) {   /* port: the title and the high scores in turn */
        lander.state_timer = 0;
        lander.title_page ^= 1;
        hud_dirty = 1;
    }
}

static void input(void)
{
    read_input();
    if (lander.state == LS_ENTRY) return;            /* the initials editor reads the keys itself */
    if (kpressed & K_HELP) { lander.help = (lander.help + 1) % 3; hud_dirty = 1; return; }
    if (lander.help) {
        if ((pressed & (IN_FIRE | IN_START)) || (kpressed & K_ESC)) { lander.help = 0; hud_dirty = 1; }
        return;
    }
    if ((kpressed & K_ESC) && lander.state != LS_TITLE) { to_title(); return; }
    switch (lander.state) {
    case LS_TITLE:
        if (pressed & IN_LEFT) { lander.menu = (lander.menu + 3) % 4; hud_dirty = 1; }
        if (pressed & IN_RIGHT) { lander.menu = (lander.menu + 1) % 4; hud_dirty = 1; }
        if (pressed & (IN_FIRE | IN_START)) start_game(lander.menu, 1);
        break;
    case LS_PLAY:
        if (kpressed & K_PAUSE) { lander.paused = !lander.paused; hud_dirty = 1; }
        if (kpressed & K_THR_UP) { lander.steady += FX(0.1); if (lander.steady > FONE) lander.steady = FONE; }
        if (kpressed & K_THR_DN) { lander.steady -= FX(0.1); if (lander.steady < 0) lander.steady = 0; }
        if (kpressed & K_AUTO) {
            lander.demo = !lander.demo;
            LOGIC();                                 /* (game_rules.c is in the rules' bank) */
            rules_autopilot_reset();
            hud_dirty = 1;
            zx_log("ld: autopilot %u\n", lander.demo);
        }
        break;
    case LS_RESULT:
        if (lander.state_timer < 75 && (pressed & (IN_FIRE | IN_START))) after_result();
        break;
    case LS_OVER:
        if (lander.state_timer == 0 && (pressed & (IN_FIRE | IN_START))) {
            ed_place = hs_rank((uint32_t)lander.score);
            if (ed_place >= 0 && lander.score > 0) {
                hs_edit_begin(&ed);
                set_state(LS_ENTRY);
            } else to_title();
        }
        break;
    }
}

static uint16_t rgb9(uint16_t c) { return (uint16_t)(((c >> 9) & 7) << 6 | ((c >> 5) & 7) << 3 | ((c >> 1) & 7)); }

/* render.c render_palette(): the world's pens in Layer 2, and its panels */
static void palette(uint8_t w)
{
    uint8_t k;
    for (k = 0; k < 32; k++) pal_set(1, k, rgb9(ld_palettes[w][k]));
    HUD();
    hud_world(w);
    LOGIC();
}

void game_init(void)
{
    in2_init();
    input_init();
    input_latch(1);
    zx_log("ld: ready kempston=%u\n", in2_has_kemp[0]);
    if (key_down(IN_KEY_SCANCODE_t)) { LOGIC(); selftest(); }
    hs_init(50);
    lander.menu = 0;
    to_title();
}

/* ---------------------------------------------------------------- a pass */
static uint16_t fps_frames, fps_loops, fps_ticks;

void game_frame(uint8_t frames)
{
    uint8_t steps;
    input();
    if (lander.state == LS_ENTRY) {
        uint8_t r;
        HUD();
        input_read();                            /* (the editor's letters: zxgame.h input) */
        r = hs_edit_step(&ed);
        if (r & HS_EDIT_TAKEN) snd_play(SFX_KEY);
        if (r & HS_EDIT_CHANGED) hud_dirty = 1;
        if (r & HS_EDIT_DONE) {
            hs_insert(ed_place, ed.name, (uint32_t)lander.score);
            zx_log("ld: hiscore %s %ld place=%d\n", ed.name, lander.score, ed_place + 1);
            to_title();
            lander.title_page = 1;
        }
    }
    acc += frames;
    if (acc > 20) acc = 20;                      /* port: at most 10 ticks a pass (the original: 4, at 25 frames a
                                                    second; here a pass takes 6-20 frames) */
    steps = acc >> 1;
    acc &= 1;
    fps_ticks += steps;
    PROF(PR_RULES);
    while (steps--) tick();
    PROF(PR_HUDV);
    /* port: the HUD's values every other pass (they are text) */
    if (lander.state != LS_TITLE && lander.state != LS_ENTRY && ((fps_loops & 1) || lander.state != LS_PLAY)) {
        LOGIC();
        hud_values();
    }
    if (!lander.help && lander.state != LS_ENTRY) {
        LOGIC();
        PROF(PR_CAM);
        view_camera(&level);
        view_compass = lander.state != LS_TITLE;
        VIEW();
        view_draw(&level, (uint8_t)lander.frame);
    }
    PROF(PR_TEXT);
    HUD();
    screens_draw();
    PROF(PR_OTHER);
    fps_loops++;
    fps_frames += frames;
    while (frames--) snd_tick();
    if (fps_frames >= 100) {
        zx_log("ld: perf fps10=%u ticks=%u frames=%u polys=%u agl=%ld state=%u ay=%u,%u,%u\n",
               (uint16_t)(fps_loops * 500u / fps_frames), fps_ticks, fps_frames, view_polys, FINT(hv.agl), lander.state, snd_reg(0, 8) & 15, snd_reg(1, 8) & 15,
               snd_reg(2, 8) & 15);
#ifdef LD_PROF
        {
            uint8_t q;
            zx_log("ld: prof");
            for (q = 0; q < PR_N; q++) { zx_log(" %u", prof_sum[q]); prof_sum[q] = 0; }
            zx_log(" loops=%u\n", fps_loops);
        }
#endif
        fps_frames = fps_loops = fps_ticks = 0;
    }
}
