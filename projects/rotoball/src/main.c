/*
 * Rotoball for the Spectrum Next: start-up, controls, the match flow and
 * the 50 Hz loop (the original's main.c main()).
 *
 * Memory: this program at 0x6000-0xEFFF (the SDK's IM 2 table and the
 * stack above): 0x6000 is the upper 8K page (11) of bank 5, free because
 * the ULA is off and the tilemap uses only page 10 and the start of 11
 * (the map at 0x4000, the tile definitions from 0x4A00: keep them below
 * 176 tiles, hud.c uses 121). Layer 2's 320 x 256 pitch in banks 16-20 and
 * the sprite images in banks 21-28, both loaded by the .nex
 * (tools/gen_gfx.py); the sound streams in bank 30. Slot 2 (page 10, the
 * tilemap) is where nexttile, aystream and view.c map other pages for a
 * moment.
 *
 * Each frame: the frame interrupt, then view_commit() shows the picture
 * worked out last frame (sprites, the floor's palettes, the copper list)
 * while the beam is in the top border; the logic runs once per frame that
 * passed (fixed timestep, at most 4), then the HUD and view_prepare().
 *
 * Controls (port: the original has W/S/A/D for its one player):
 *   P1  W/S drive, A/D strafe; Sinclair 2 (1-5); Kempston 1; SPACE
 *   P2  I/K drive, J/L strafe; Sinclair 1 (6-0); Kempston 2
 * Against the CPU, P1 may use either set.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "input2.h"
#include "nextgfx.h"
#include "roto.h"
#include "view.h"
#include "hud.h"
#include "sound.h"
#include "gfx_data.h"

#define SRC_P1 (IN2_WASD | IN2_SINCLAIR2 | IN2_KEMPSTON1 | IN2_SPACE | IN2_ENTER)
#define SRC_P2 (IN2_IJKL | IN2_SINCLAIR1 | IN2_KEMPSTON2)

static in2_pad pad[2] = {{SRC_P1, 0, 0}, {SRC_P2, 0, 0}};
static uint8_t sel = 1;                 /* the title's choice: players */
static uint16_t state_frames;           /* steps since the state changed */

/* profiling: raster lines spent per part (a line is ~1800 T-states at
   28 MHz), logged every 250 frames with the overruns (missed frames) */
static uint16_t raster(void) { return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F); }
static uint16_t prof_t, prof[4], prof_max, frame_t0;
static uint8_t prof_n;
static void lap(uint8_t k)
{
    uint16_t t = raster();
    prof[k] += t >= prof_t ? t - prof_t : t + 312 - prof_t;
    prof_t = t;
}

static uint8_t controls(uint8_t in)
{
    uint8_t c = 0;
    if (in & IN_UP) c |= C_FWD;
    if (in & IN_DOWN) c |= C_BACK;
    if (in & IN_LEFT) c |= C_LEFT;
    if (in & IN_RIGHT) c |= C_RIGHT;
    return c;
}

static void step(void)
{
    uint8_t pressed = pad[0].pressed | pad[1].pressed;
    state_frames++;
    if (G.state == MS_TITLE) {
        if (pressed & (IN_UP | IN_LEFT)) sel = 1;
        if (pressed & (IN_DOWN | IN_RIGHT)) sel = 2;
        if (pressed & (IN_UP | IN_LEFT | IN_DOWN | IN_RIGHT)) snd_sfx(ST_BLIP);
        if (pressed & (IN_FIRE | IN_START)) {
            snd_title_music(0);
            snd_sfx(ST_WHISTLE);
            game_start(sel);
            pad[0].src = sel == 1 ? SRC_P1 | SRC_P2 : SRC_P1;
            state_frames = 0;
            zx_log("roto: start players=%u\n", sel);
        }
    } else if (G.state == MS_PLAYING) {
        game_step(controls(pad[0].held), controls(pad[1].held));
        if (G.events & (EV_PICKUP | EV_TACKLE)) snd_sfx(ST_PING);
        if (G.events & EV_TACKLE) zx_log("roto: tackle to=P%u\n", G.ball.carrier + 1);
        if (G.events & (EV_GOAL1 | EV_GOAL2)) {
            snd_sfx(ST_CLANG);
            zx_log("roto: goal P%u pts=%u score=%d:%d step=%u\n", G.last_scorer + 1, G.last_pts, G.score[0],
                   G.score[1], G.steps);
        }
        if (G.events & EV_FULL) {
            snd_jingle();
            state_frames = 0;
            zx_log("roto: fulltime score=%d:%d players=%u\n", G.score[0], G.score[1], G.players);
        }
    } else if (state_frames > 50 && (pressed & (IN_FIRE | IN_START))) {
        G.state = MS_TITLE;              /* full time -> the title */
        pad[0].src = SRC_P1;
        state_frames = 0;
        snd_title_music(1);
        zx_log("roto: title\n");
    }
}

int main(void)
{
    uint8_t ticks, i;
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x33);          /* sprites on, over the border, clipped there; order U S L */
    l2w_init(L2_BANK);
    hud_init();
    snd_init();
    in2_init();
    game_init();
    view_init();
    frame_init();
    snd_title_music(1);
    zx_log("roto: ready kempston=%u,%u\n", in2_has_kemp[0], in2_has_kemp[1]);

    for (;;) {
        ticks = frame_wait();
        prof_t = frame_t0 = raster();
        view_commit();
        lap(0);
        if (ticks > 4) ticks = 4;
        in2_scan();
        in2_update(&pad[0]);
        in2_update(&pad[1]);
        for (i = 0; i < ticks; i++) {
            step();
            pad[0].pressed = pad[1].pressed = 0;   /* a press counts once */
            snd_tick();
        }
        probe_update();
        lap(1);
        hud_update(sel);
        lap(2);
        view_prepare();
        lap(3);
        {
            uint16_t t = raster(), d = t >= frame_t0 ? t - frame_t0 : t + 312 - frame_t0;
            if (d > prof_max) prof_max = d;
        }
        if (++prof_n == 250) {
            zx_log("roto: perf lines/250: commit=%u logic=%u hud=%u prepare=%u worst=%u overruns=%u copper=%u\n",
                   prof[0], prof[1], prof[2], prof[3], prof_max, frame_overruns, view_copper_n);
            prof[0] = prof[1] = prof[2] = prof[3] = 0;
            prof_n = 0;
            prof_max = 0;
        }
    }
    return 0;
}
