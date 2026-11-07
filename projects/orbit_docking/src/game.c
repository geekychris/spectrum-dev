/*
 * game.c - Orbit Docking's game shell for the Next (the original's game.c
 * and the text parts of render.c): the title with the autopilot docking
 * behind it, play, game over, the help pages, sound, and here the high
 * scores.
 *
 * The rules run at the original's 25 ticks a second whatever the drawing
 * costs: each pass of the main loop runs a tick for every two frames that
 * went by (fixed timestep, at most 4 a pass, as the original), then the
 * picture catches up.
 *
 * port: keys. Across: Q A O P, the cursor keys, a Sinclair or Kempston
 * joystick; in and out: W / S; roll: K / L (the original's A / D: A is
 * down here); with fire held the joystick's up / down go in and out and
 * left / right roll (as the original's). ENTER pauses (the original's P
 * is right on QAOP), B the autopilot, H the help, BREAK (the original's
 * Esc) back to the title.
 * port: the high-score table and the initials (the original keeps a best
 * score only).
 */
#include <string.h>
#include <stdio.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "sound.h"
#include "screens.h"

Dock dk;
ODGame game;
static uint32_t next_seed = 1;
static uint8_t acc, prev_keys, fire_lock, thrust_timer;
uint8_t hud_dirty = 1;                   /* screens.c redraws everything */
int16_t scr_err;                         /* od_roll_error(), for screens.c (the rules' bank is out then) */
static unsigned play_in;                 /* the rules' input bits */
hs_editor ed;
int8_t ed_place;
static char tmp[40];

static const char *const state_names[] = { "TITLE", "FLY", "OVER", "ENTRY" };

static void set_state(uint8_t s)
{
    dk.state = s;
    hud_dirty = 1;
    zx_log("od: state %s level=%d score=%ld docks=%d crashes=%d\n", state_names[s], game.level, game.score, game.docks,
           game.crashes);
}

static void flash(const char *s, uint8_t ink)
{
    strncpy(dk.flash, s, sizeof(dk.flash) - 1);
    dk.flash_timer = 40;
    dk.flash_ink = ink;
    hud_dirty = 1;
}

static void start_game(uint8_t level)
{
    od_new_game(&game, next_seed++, level);
    dk.paused = 0;
    dk.help = 0;
    dk.autoplay = 0;
    fire_lock = 1;
    snd_music(MUS_NONE);
    set_state(DS_PLAY);
    zx_log("od: start level %u seed %lu\n", level, game.seed);
}

static void to_title(void)
{
    od_new_game(&game, 777 + next_seed, 1);
    dk.title_page = 0;
    dk.state_timer = 0;
    snd_music(MUS_TITLE);
    set_state(DS_TITLE);
}

/* the original's determinism test (tests/test_cases.c): the autopilot's
   long game from station 1, seed 1234, whose state hash is its
   tests/vectors.h's (0xEFE8A63F). Runs at start-up while T is held (test.py) */
static void selftest(void)
{
    int16_t k;
    od_new_game(&game, 1234, 1);
    for (k = 0; k < 6000 && !game.over && !game.won; k++) od_tick(&game, od_bot(&game));
    zx_log("od: selftest ticks=%d level=%d docks=%d crashes=%d score=%ld hash=%08lX\n", k, game.level, game.docks,
           game.crashes, game.score, od_state_hash(&game));
}

void game_init(void)
{
    if (key_down(IN_KEY_SCANCODE_t)) selftest();
    dk.start_level = 1;
    hs_init(1000);
    to_title();
}

/* the original's handle_events(): sounds and messages (quiet behind the title) */
static void events(void)
{
    ODEvent e;
    uint8_t quiet = dk.state == DS_TITLE;
    while (od_pop_event(&game, &e)) {
        if (quiet) continue;
        switch (e.type) {
        case ODE_DOCK: snd_play(SFX_DOCK); break;
        case ODE_CRASH: case ODE_DEBRIS:
            snd_play(SFX_CRASH);
            sprintf(tmp, "CRASH: %s", od_fail_names[e.a]);
            flash(tmp, TX_RED);
            break;
        case ODE_FUEL_OUT: snd_play(SFX_FUEL); flash("OUT OF FUEL", TX_RED); break;
        case ODE_OVER: snd_play(SFX_OVER); break;
        case ODE_WIN: snd_play(SFX_WIN); break;
        }
        zx_log("od: ev %s a=%ld b=%ld score=%ld level=%d lives=%d\n", od_event_names[e.type], e.a, e.b, game.score,
               game.level, game.lives);
    }
}

/* ---- input ---- */
#define K_HELP 1
#define K_AUTO 2
#define K_FWD  4
#define K_BACK 8
#define K_RL   16
#define K_RR   32

static uint8_t keys_now(void)
{
    uint8_t keys = 0;
    if (key_down(IN_KEY_SCANCODE_h)) keys |= K_HELP;
    if (key_down(IN_KEY_SCANCODE_b)) keys |= K_AUTO;
    if (key_down(IN_KEY_SCANCODE_w)) keys |= K_FWD;
    if (key_down(IN_KEY_SCANCODE_s)) keys |= K_BACK;
    if (key_down(IN_KEY_SCANCODE_k)) keys |= K_RL;
    if (key_down(IN_KEY_SCANCODE_l)) keys |= K_RR;
    return keys;
}

/* the frame interrupt: the joystick keys latched (input_sample), and
   these: a tap shorter than a pass of the loop still counts */
static volatile uint8_t keys_latched;

void game_sample(void)
{
    input_sample();
    keys_latched |= keys_now();
}

static void input(void)
{
    uint8_t held = input_read(), edge = input_pressed(), keys, kedge;
    keys = keys_now();
    kedge = (keys | keys_latched) & ~prev_keys;
    keys_latched = 0;
    prev_keys = keys;
    if (dk.state == DS_ENTRY) return;            /* the initials editor reads the keys itself */
    if (kedge & K_HELP) {
        dk.help = (dk.help + 1) % 3;
        hud_dirty = 1;
        return;
    }
    if (dk.help) {
        if (edge & (IN_FIRE | IN_START | IN_QUIT)) { dk.help = 0; hud_dirty = 1; }
        return;
    }
    if ((edge & IN_QUIT) && dk.state != DS_TITLE) {
        to_title();
        return;
    }
    play_in = 0;
    switch (dk.state) {
    case DS_TITLE:
        if ((edge & IN_UP) && dk.start_level < OD_LEVELS) { dk.start_level++; hud_dirty = 1; }
        if ((edge & IN_DOWN) && dk.start_level > 1) { dk.start_level--; hud_dirty = 1; }
        if (edge & (IN_FIRE | IN_START)) start_game(dk.start_level);
        break;
    case DS_PLAY:
        if (edge & IN_START) { dk.paused = !dk.paused; hud_dirty = 1; }
        if (kedge & K_AUTO) {
            dk.autoplay = !dk.autoplay;
            hud_dirty = 1;
            zx_log("od: autoplay %u\n", dk.autoplay);
        }
        if (!(held & IN_FIRE)) fire_lock = 0;
        if ((held & IN_FIRE) && !fire_lock)      /* the joystick: fire shifts up/down to in/out, left/right to roll */
            play_in = (held & IN_UP ? OD_IN_FWD : 0) | (held & IN_DOWN ? OD_IN_BACK : 0) |
                      (held & IN_LEFT ? OD_IN_ROLL_L : 0) | (held & IN_RIGHT ? OD_IN_ROLL_R : 0);
        else
            play_in = (held & IN_LEFT ? OD_IN_LEFT : 0) | (held & IN_RIGHT ? OD_IN_RIGHT : 0) |
                      (held & IN_UP ? OD_IN_UP : 0) | (held & IN_DOWN ? OD_IN_DOWN : 0);
        play_in |= (keys & K_FWD ? OD_IN_FWD : 0) | (keys & K_BACK ? OD_IN_BACK : 0) |
                   (keys & K_RL ? OD_IN_ROLL_L : 0) | (keys & K_RR ? OD_IN_ROLL_R : 0);
        break;
    case DS_OVER:
        if (dk.state_timer == 0 && (edge & (IN_FIRE | IN_START))) {
            ed_place = hs_rank((uint32_t)game.score);
            if (ed_place >= 0 && game.score > 0) {
                hs_edit_begin(&ed);
                set_state(DS_ENTRY);
            } else to_title();
        }
        break;
    }
}

/* ---- the rules, 25 ticks a second (the original's game_update) ---- */
static void tick(void)
{
    unsigned bits = 0;
    if (dk.help || dk.paused || dk.state == DS_ENTRY) return;
    if (dk.state == DS_TITLE) {
        if (game.over || game.won || game.between == 1)
            od_new_game(&game, game.seed + 1, 1 + (int)(game.seed % 3));
        od_tick(&game, od_bot(&game));
        if (++dk.state_timer >= 200) {           /* port: the title and the high scores in turn */
            dk.state_timer = 0;
            dk.title_page ^= 1;
            hud_dirty = 1;
        }
    } else if (dk.state == DS_PLAY) {
        bits = dk.autoplay ? od_bot(&game) : play_in;
        od_tick(&game, bits);
    }
    if (bits && game.fuel > 0 && --thrust_timer == 0) { snd_play(SFX_THRUST); thrust_timer = 6; }
    if (!bits) thrust_timer = 1;
    events();
    if (dk.flash_timer > 0 && --dk.flash_timer == 0) hud_dirty = 1;
    if (dk.state == DS_OVER && dk.state_timer > 0 && --dk.state_timer == 0) hud_dirty = 1;
    if (dk.state == DS_PLAY && (game.over || game.won)) {
        dk.state_timer = 40;
        set_state(DS_OVER);
    }
}

/* ---- the main loop's pass ---- */
static uint16_t fps_frames, fps_loops, fps_ticks;

void game_frame(uint8_t frames)
{
    uint8_t steps, k;
    LOGIC();
    input();
    if (dk.state == DS_ENTRY) {
        uint8_t r = hs_edit_step(&ed);
        if (r & HS_EDIT_TAKEN) snd_play(SFX_KEY);
        if (r & HS_EDIT_CHANGED) hud_dirty = 1;
        if (r & HS_EDIT_DONE) {
            hs_insert(ed_place, ed.name, (uint32_t)game.score);
            zx_log("od: hiscore %s %ld place=%d\n", ed.name, game.score, ed_place + 1);
            to_title();
            dk.title_page = 1;
        }
    }
    acc += frames;
    if (acc > 8) acc = 8;                        /* at most 4 ticks a pass, as the original */
    steps = acc >> 1;
    acc &= 1;
    fps_ticks += steps;
    while (steps--) tick();
    scr_err = od_roll_error(&game);
    HUD();
    screens_draw();
    LOGIC();
    for (k = 0; k < game.ndebris; k++) view_debris_x[k] = (int16_t)(od_debris_x(&game, k) >> 16);
    if (!dk.help && dk.state != DS_ENTRY) {
        VIEW();
        view_draw(&game);
        LOGIC();
    }
    fps_loops++;
    fps_frames += frames;
    while (frames--) snd_tick();
    if (fps_frames >= 100) {
        zx_log("od: perf fps10=%u ticks=%u polys=%u z=%ld level=%d docks=%d ay=%u,%u,%u\n",
               (uint16_t)(fps_loops * 500u / fps_frames), fps_ticks, view_polys, game.z >> 16, game.level, game.docks,
               snd_reg(0, 8) & 15, snd_reg(1, 8) & 15, snd_reg(2, 8) & 15);
        fps_frames = fps_loops = fps_ticks = 0;
    }
}
