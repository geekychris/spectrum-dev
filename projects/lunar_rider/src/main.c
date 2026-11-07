/*
 * Lunar Rider for the Spectrum Next - port of amiga_mcp/examples/lunar_rider.
 *
 * game.c is the original game (terrain, the buggy's physics, enemies,
 * bombs, checkpoints, scoring); view.c draws it with the Next's layers;
 * this file is the main loop (the original's main.c): a fixed 50 Hz
 * timestep (the logic runs once per frame that passed, at most 4), input,
 * sound, the title, and the high scores (port: the original has none).
 *
 * Controls (as the original): O / P or joystick left / right: slow down /
 * speed up; Q, SPACE or joystick up: jump; A or fire: shoot (forward and
 * up). Kempston and Sinclair joysticks through the SDK.
 *
 * Memory:
 *   0x0000-0x3FFF  bank 22: the original's game logic (game.c), over the ROM
 *                  (nothing here uses the ROM: text is the 5 x 7 font)
 *   0x4000-0x5FFF  bank 5's lower page: the tilemap (map at 0x4000, 36 tile
 *                  definitions from 0x4A00); Layer 2 columns and other pages
 *                  are mapped here for a moment
 *   0x6000-0xEFFF  the program, the SDK, the C library (bank 5's upper page:
 *                  the ULA is off), then the IM 2 table and the stack
 *   banks 16-20    Layer 2 (320 x 256); bank 21 the sprite patterns (.nex)
 */
#include <stdint.h>
#include <string.h>
#include <input.h>
#include <arch/zxn.h>
#include "game.h"
#include "view.h"
#include "sound.h"
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"

GameState gs;                           /* global: test.py reads it by symbol */
static InputState in;
static uint16_t startup_delay = 30;     /* suppress input at start (the original's) */

/* for test bots: what lies ahead of the buggy, written once a frame */
struct {
    uint32_t ground_x;                  /* world x at the screen's left edge */
    int16_t buggy_y, vy;
    uint8_t on_ground, alive, state, lives;
    uint8_t ahead[112];                 /* terrain type from the buggy's left edge on */
} lunar_probe;

static void probe(void)
{
    uint32_t gx = (uint32_t)gs.scroll_x / 100;
    WORD i = (WORD)((gx + BUGGY_X) % TERRAIN_LEN);
    uint8_t k, *a = lunar_probe.ahead;
    lunar_probe.ground_x = gx;
    lunar_probe.buggy_y = gs.buggy.y;
    lunar_probe.vy = gs.buggy.vy;
    lunar_probe.on_ground = (uint8_t)gs.buggy.on_ground;
    lunar_probe.alive = (uint8_t)gs.buggy.alive;
    lunar_probe.state = (uint8_t)gs.state;
    lunar_probe.lives = (uint8_t)gs.lives;
    for (k = 0; k < sizeof(lunar_probe.ahead); k++) {
        *a++ = (uint8_t)gs.terrain[i];
        if (++i == TERRAIN_LEN) i = 0;
    }
}

/* ------------------------------------------------------------ input */

static void read_input(void)
{
    uint8_t b = input_read();
    uint8_t space = key_down(IN_KEY_SCANCODE_SPACE);
    in.left = (b & IN_LEFT) != 0;
    in.right = (b & IN_RIGHT) != 0;
    in.jump = (b & IN_UP) != 0 || space;
    in.fire = ((b & IN_FIRE) && !space) || (b & IN_DOWN);      /* A or fire */
    in.quit = 0;
}

/* ------------------------------------------------------------ log */

static const char *const state_name[5] = {"TITLE", "PLAYING", "DYING", "GAMEOVER", "CHECKPOINT"};

static void log_state(void)
{
    zx_log("lunar: state=%s score=%ld lives=%d zone=%d x=%ld\n", state_name[gs.state], gs.score, gs.lives,
           gs.checkpoint_cur + 1, gs.scroll_x / 100);
}

/* profiling: raster lines spent a frame (a line is ~1,800 T-states at 28 MHz) */
static uint16_t raster(void) { return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F); }

/* ------------------------------------------------------------ high scores */

static void initials(void)
{
    hs_editor ed;
    uint8_t r = HS_EDIT_CHANGED;
    int8_t place = hs_rank((uint32_t)gs.score);
    if (place < 0) return;
    zx_log("lunar: hiscore rank=%d\n", place);
    view_initials(0, 0, gs.score);
    hs_edit_begin(&ed);
    while (!(r & HS_EDIT_DONE)) {
        if (r & HS_EDIT_CHANGED) view_initials(ed.name, ed.pos, gs.score);
        if (r & HS_EDIT_TAKEN) snd_shoot();
        frame_wait();
        snd_tick();
        input_read();
        r = hs_edit_step(&ed);
    }
    hs_insert(place, ed.name, (uint32_t)gs.score);
    zx_log("lunar: name=%s\n", ed.name);
}

static void to_title(void)
{
    gs.state = STATE_TITLE;
    gs.frame = 0;
    view_title(0);
    zx_log("lunar: title\n");
}

/* ------------------------------------------------------------ main loop */

int main(void)
{
    static const char *const names[HS_ENTRIES] = {"LRF", "MBG", "UFO", "CPT", "ZNE"};
    uint16_t perf_frames = 0, perf_draws = 0, perf_ticks = 0, perf_lines = 0, perf_worst = 0;
    uint8_t i;
    long last_score = -1;

    ZXN_WRITE_MMU0(44);                 /* bank 22: game.c (see above) */
    ZXN_WRITE_MMU1(45);
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x23);          /* sprites on, over the border, clipped there; order S L U */
    input_init();
    input_fast();
    ay_init();
    snd_init();
    hs_init(200);                       /* port: a table (the original keeps no scores) */
    for (i = 0; i < HS_ENTRIES; i++) memcpy(hs_table[i].name, names[i], 4);
    view_init();
    game_init(&gs);
    gs.state = STATE_TITLE;
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    view_title(0);
    zx_log("lunar: ready kempston=%u\n", input_kempston);

    for (;;) {
        uint8_t ticks = frame_wait(), tick;
        uint16_t t0;
        WORD prev = gs.state;
        perf_frames += ticks;
        if (ticks > 4) ticks = 4;
        t0 = raster();
        if (gs.state != STATE_TITLE) view_commit();     /* the copper list, in the border */
        read_input();

        for (tick = 0; tick < ticks; tick++) {
            InputState use = in;
            /* Startup delay - suppress all input */
            if (startup_delay > 0) {
                startup_delay--;
                memset(&use, 0, sizeof(use));
            }
            if (gs.state == STATE_TITLE) {
                gs.frame++;
                if ((gs.frame % 20) == 0) view_title_blink((gs.frame / 20) & 1);
                if (use.fire || use.jump) {
                    game_init(&gs);
                    gs.state = STATE_PLAYING;
                    view_play();
                    log_state();
                    break;
                }
            }
            else {
                game_update(&gs, &use);
                if (gs.ev_shoot) snd_shoot();
                if (gs.ev_explode) snd_explode();
                if (gs.ev_jump) snd_jump();
                if (gs.ev_checkpoint) {
                    snd_checkpoint();
                    zx_log("lunar: checkpoint zone=%d score=%ld x=%ld\n", gs.checkpoint_cur + 1, gs.score,
                           gs.scroll_x / 100);
                }
                if (gs.ev_death) {
                    snd_death();
                    static const char *const cause[6] = {"?", "crater", "rock", "mine", "bomb", "enemy"};
                    zx_log("lunar: death lives=%d cause=%s score=%ld x=%ld y=%d\n", gs.lives,
                           cause[gs.death_cause < 6 ? gs.death_cause : 0], gs.score, gs.scroll_x / 100, gs.buggy.y);
                }
                /* port: the original's main loop: fire or jump leaves the game
                   over screen (here a new press, after a second), then the
                   high-score entry */
                if (gs.state == STATE_GAMEOVER && gs.state_timer < 130 &&
                    (input_pressed() & (IN_FIRE | IN_UP | IN_DOWN)))
                    gs.state = STATE_TITLE;
            }
            snd_tick();
            perf_ticks++;
            if (gs.state != prev) break;
        }

        if (gs.state != prev) {
            log_state();
            if (gs.state == STATE_GAMEOVER) {
                view_frame();
                view_gameover();
                zx_log("lunar: gameover score=%ld zone=%d overruns=%u\n", gs.score, gs.checkpoint_cur + 1,
                       frame_overruns);
            }
            else if (gs.state == STATE_TITLE && prev != STATE_TITLE) {
                initials();
                to_title();
                last_score = -1;
            }
        }
        if (gs.state != STATE_TITLE && gs.state != STATE_GAMEOVER) {
            view_frame();
            perf_draws++;
        }
        if (gs.score != last_score && gs.state != STATE_TITLE) {
            zx_log("lunar: score=%ld\n", gs.score);
            last_score = gs.score;
        }
        probe();
        {
            uint16_t t = raster(), d = t >= t0 ? t - t0 : t + 312 - t0;
            perf_lines += d;
            if (d > perf_worst) perf_worst = d;
        }
        if (perf_frames >= 100) {
            zx_log("lunar: perf draws=%u frames=%u ticks=%u lines=%u worst=%u state=%s\n", perf_draws,
                   perf_frames, perf_ticks, perf_lines, perf_worst, state_name[gs.state]);
            zx_log("lunar: ay mix0=%u vol0=%u mix1=%u\n", snd_reg(0, 7), snd_reg(0, 8), snd_reg(1, 7));
            perf_frames = perf_draws = perf_ticks = perf_lines = perf_worst = 0;
        }
    }
    return 0;
}
