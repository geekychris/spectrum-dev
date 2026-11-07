/*
 * Uranus Lander for the ZX Spectrum 48K/128K - port of
 * amiga_mcp/examples/uranus_lander.
 *
 * game.c is the original game (terrain, pads, physics, landing rules,
 * scoring, levels); view.c draws it for the Spectrum; this file is the main
 * loop: a fixed 50 Hz timestep (the logic runs once for every frame that
 * passed, the screen is drawn once), input (keys, Kempston, Sinclair),
 * the autopilot, sound (beeper; the AY on a 128K) and the high-score table.
 *
 * Keys: O/P rotate, Q or fire (SPACE, M, 0, joystick) thrust, Z autopilot
 * on/off, T music on/off (128K).
 */
#include <stdint.h>
#include <string.h>
#include <input.h>
#include <arch/zx.h>
#include "game.h"
#include "view.h"
#include "autopilot.h"
#include "zxgame.h"
#include "zxlog.h"

GameState gs;                       /* global: test.py reads it by symbol */
static InputState in;
static uint8_t autopilot;
static uint8_t music_on = 1;
static WORD startup_delay = 30;
/* sound events of the ticks run since the last frame */
static uint8_t ev_thrust, ev_crash, ev_land, ev_low_fuel;

/* ------------------------------------------------------------ sound */

/* a slow arpeggio in place of the original's MOD (AY only) */
static ay_note melody[33];         /* filled by music_init (AY_NOTE is a table) */
static void music_init(void)
{
    static const uint8_t notes[32] = {
        45, 52, 57, 60, 64, 60, 57, 52,   41, 48, 53, 57, 60, 57, 53, 48,
        43, 50, 55, 59, 62, 59, 55, 50,   40, 47, 52, 55, 59, 55, 52, 47,
    };
    uint8_t i;
    for (i = 0; i < 32; i++) {
        melody[i].period = AY_NOTE(notes[i]);
        melody[i].len = 9;
    }
    melody[32].period = 0;
    melody[32].len = 0;
}

static uint8_t thrust_sounding;

static void sound_frame(void)
{
    if (ay_present) {
        if (ev_thrust && !thrust_sounding) ay_noise(0, 24, 9);   /* a rumble while thrusting */
        else if (!ev_thrust && thrust_sounding) ay_off(0);
        thrust_sounding = ev_thrust;
        if (ev_crash) ay_sfx(1, 0, 15, 1, 0);
        if (ev_land) ay_sfx(1, 400, 15, 1, -12);
        if (ev_low_fuel) ay_sfx(1, 90, 12, 3, 0);
        sound_tick();
        return;
    }
    /* beeper: it stops the CPU, so only short sounds */
    if (ev_crash) beep_noise(700);
    else if (ev_land) beep_sweep(160, 40, 20, 6);
    else if (ev_low_fuel) beep(70, 25);
    else if (ev_thrust) beep_noise(24);
    if (gs.state == STATE_CRASHING && gs.state_timer > 40 && (gs.state_timer & 3) == 0)
        beep_noise(gs.state_timer * 2);
}

static void sound_quiet(void)
{
    if (!ay_present) return;
    ay_off(0);
    thrust_sounding = 0;
}

/* ------------------------------------------------------------ log */

static const char *const state_name[6] = {"TITLE", "PLAYING", "LANDED", "CRASHING", "GAMEOVER", "ENTER_NAME"};

static void log_level(void)
{
    uint16_t sum = 0, run = 0, i;
    /* sum of terrain_y[i] * (i + 1), without multiplies: suffix sums */
    for (i = TERRAIN_W; i--;) {
        run += (uint16_t)gs.terrain_y[i];
        sum += run;
    }
    zx_log("uranus: level=%d terrain=%u pads=", gs.level, sum);
    for (i = 0; i < (uint16_t)gs.num_pads; i++) {
        LandingPad *p = &gs.pads[i];
        zx_log("%d,%d,%d,%d;", p->x, p->width, p->y, p->multiplier);
    }
    zx_log(" gravity=%ld fuel=%ld\n", g_tune.gravity, g_tune.fuel_max);
}

static void log_state(void)
{
    zx_log("uranus: state=%s level=%d score=%ld lives=%d\n",
           state_name[gs.state], gs.level, gs.score, gs.lives);
}

/* ------------------------------------------------------------ transitions */

static void enter_state(WORD prev)
{
    log_state();
    switch (gs.state) {
    case STATE_PLAYING:
        if (prev == STATE_TITLE || prev == STATE_LANDED) {
            view_play_screen();
            log_level();
            autopilot_reset();
        }
        else view_overlay_clear();                     /* after a crash */
        break;
    case STATE_LANDED:
        zx_log("uranus: landed level=%d pad=%d x%d bonus=%ld score=%ld vx=%ld vy=%ld angle=%d\n",
               gs.level, gs.landed_pad, gs.pads[gs.landed_pad].multiplier, gs.land_bonus, gs.score,
               gs.ship.vx, gs.ship.vy, gs.ship.angle);
        view_landed();
        break;
    case STATE_CRASHING:
        zx_log("uranus: crash level=%d score=%ld lives=%d\n", gs.level, gs.score, gs.lives);
        view_crash();
        break;
    case STATE_GAMEOVER:
        zx_log("uranus: gameover score=%ld overruns=%u\n", gs.score, frame_overruns);
        sound_quiet();
        if (ay_present) music_stop();
        view_gameover();
        break;
    case STATE_ENTER_NAME:
        /* port: the SDK's initials screen, then the title */
        hs_enter(hs_rank((uint32_t)gs.score), (uint32_t)gs.score, "uranus");
        gs.state = STATE_TITLE;
        /* fall through */
    case STATE_TITLE:
        autopilot = view_autopilot = 0;
        view_title();
        if (ay_present && music_on) music_play(2, melody, 9);
        zx_log("uranus: title\n");
        break;
    }
}

/* ------------------------------------------------------------ main loop */

int main(void)
{
    static const char *const names[HS_ENTRIES] = {"ACE", "ZAP", "BOB", "JOE", "TOM"};
    uint8_t i, t_held = 0;
    uint16_t perf_frames = 0, perf_draws = 0, perf_ticks = 0;

    gfx_init();
    input_init();
    input_fast();                   /* table key decode: ~9,000 T-states less a read */
    ay_init();
    /* port: the original's top five defaults (it keeps ten) */
    hs_init(100);
    for (i = 0; i < HS_ENTRIES; i++) {
        memcpy(hs_table[i].name, names[i], 4);
        hs_table[i].score = 1000 - i * 100;
    }
    music_init();
    view_init();
    game_init(&gs);
    gs.state = STATE_TITLE;
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    view_title();
    if (ay_present) music_play(2, melody, 9);
    zx_log("uranus: ready kempston=%u ay=%u\n", input_kempston, ay_present);

    for (;;) {
        uint8_t ticks, tick, b, p;
        static WORD state_drawn = STATE_TITLE;
        uint16_t d0 = view_draws();

        ticks = frame_wait();
        perf_frames += ticks;
        if (ticks > 6) ticks = 6;       /* (a level's first frame takes longer) */

        /* draw what the last logic left, then sound */
        if (gs.state == STATE_TITLE) view_title_frame();
        else {
            view_frame();
            if (gs.state == STATE_GAMEOVER) view_blink(13, 11, 10, INK_YELLOW | BRIGHT);
        }
        sound_frame();
        ev_thrust = ev_crash = ev_land = ev_low_fuel = 0;
        perf_draws += view_draws() - d0;
        if (perf_frames >= 250) {
            zx_log("uranus: perf draws=%u frames=%u ticks=%u state=%s\n", perf_draws, perf_frames, perf_ticks,
                   state_name[gs.state]);
            perf_frames = perf_draws = perf_ticks = 0;
        }

        b = input_read();
        p = input_pressed();
        if ((p & IN_FIRE2) && gs.state == STATE_PLAYING) {
            autopilot ^= 1;
            view_autopilot = autopilot;
            autopilot_reset();
            zx_log("uranus: autopilot=%u\n", autopilot);
        }
        if (key_down(IN_KEY_SCANCODE_t)) {
            if (!t_held && ay_present) {
                music_on ^= 1;
                if (music_on && gs.state != STATE_GAMEOVER) music_play(2, melody, 9);
                else music_stop();
            }
            t_held = 1;
        }
        else t_held = 0;

        for (tick = 0; tick < ticks;) {
            WORD prev = gs.state;
            in.left = (b & IN_LEFT) != 0;
            in.right = (b & IN_RIGHT) != 0;
            in.thrust = (b & IN_FIRE) != 0;
            in.up = (b & IN_UP) != 0;
            in.down = (b & IN_DOWN) != 0;
            in.quit = 0;
            /* Suppress input during startup */
            if (startup_delay > 0) {
                startup_delay--;
                memset(&in, 0, sizeof(in));
            }
            if (gs.state == STATE_TITLE) {
                gs.frame++;
                if (in.thrust || in.up) {
                    game_init(&gs);
                    gs.state = STATE_PLAYING;
                    startup_delay = 15;
                }
            }
            else {
                if (autopilot && gs.state == STATE_PLAYING) autopilot_fly(&gs, &in);
                game_update(&gs, &in);
                ev_thrust |= gs.ev_thrust;
                ev_crash |= gs.ev_crash;
                ev_land |= gs.ev_land;
                ev_low_fuel |= gs.ev_low_fuel;
            }
            tick++;
            if (gs.state != prev) break;    /* the screen changes before more logic */
        }
        stars_update(&gs, tick);            /* the stars of the ticks run */
        perf_ticks += tick;
        if (gs.state != state_drawn) enter_state(state_drawn);
        state_drawn = gs.state;
    }
}
