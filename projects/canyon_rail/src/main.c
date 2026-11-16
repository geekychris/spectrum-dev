/*
 * Canyon Rail for the Spectrum Next: memory map, start-up and the frame loop.
 *
 *   0x0000-0x3FFF  bank 16: the rules (cr_core.c)        map_rules()
 *                  bank 18: the picture, the sprites      map_render()
 *                  bank 20: the shell (front.c)           map_front()
 *   0x4000-0x5FFF  bank 5 for the tilemap (nexttile)
 *   0x6000-0x7FFF  Layer 2 and sprite pattern pages while they are written
 *   0x8000-0xEFFF  resident: main.c, cr_place.c (where things are), the SDK,
 *                  every variable
 *   banks 40..47   the sprites' patterns (tools/gen_sprites.py)
 *
 * The rules tick at the original's 25 Hz (fixed timestep, up to 4 ticks a
 * frame); the picture is drawn once a frame.
 */
#ifndef CR_TRACE
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include <z80.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "palfx.h"
#include "zxlog.h"
#include "cr.h"
#include "render.h"
#include "sprites.h"
#include "front.h"

CRGame game;
Rail rl;
uint16_t render_polys;                  /* (render.c's, here: resident) */
uint8_t render_tracer;
unsigned play_in;
uint8_t start_stage_req;
uint8_t sfx_queue[4];

static volatile uint16_t frames50;
static volatile uint8_t keys_latched;
static uint32_t next_seed = 1;

void map_rules(void) { ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33); }    /* bank 16 */
void map_render(void) { ZXN_WRITE_MMU0(36); ZXN_WRITE_MMU1(37); }   /* bank 18 */
void map_front(void) { ZXN_WRITE_MMU0(40); ZXN_WRITE_MMU1(41); }    /* bank 20 */

/* ---------- sound: the original's synthesised effects, on the AY ---------- */
/* channel, MIDI note (0: noise), volume, decay, sweep; SFX_* order (front.h) */
static const int8_t sfx_tab[][5] = {
    { 0, 90, 12, 2, 6 },    /* shot */
    { 2, 0, 15, 1, 0 },     /* boom */
    { 1, 84, 13, 2, 3 },    /* hit */
    { 1, 91, 12, 2, -4 },   /* bolt shot down */
    { 1, 62, 9, 2, -3 },    /* enemy fire */
    { 2, 0, 15, 1, 0 },     /* hurt (and a low tone) */
    { 0, 79, 13, 1, -2 },   /* pod */
    { 0, 45, 14, 1, 0 },    /* the guardian */
    { 0, 72, 14, 1, -5 },   /* stage clear */
    { 0, 57, 14, 1, 12 },   /* game over */
    { 0, 84, 14, 1, -6 },   /* victory */
    { 1, 79, 10, 3, 0 },    /* a key */
};

void sfx(uint8_t id)
{
    uint8_t k;
    for (k = 0; k < 4; k++)
        if (!sfx_queue[k]) { sfx_queue[k] = id; return; }
}

static void on_frame(void)
{
    static uint8_t prev;                        /* (only here) */
    uint8_t k, now, edge;
    frames50++;
    input_sample();
    /* P, B, H straight from their half-rows (a 0 bit is a key down) */
    now = (z80_inp(0xDFFE) & 0x01 ? 0 : KEY_PAUSE) | (z80_inp(0x7FFE) & 0x10 ? 0 : KEY_AUTO) |
          (z80_inp(0xBFFE) & 0x10 ? 0 : KEY_HELP);
    /* (zsdcc moved a store to a volatile keys_prev above its load, which made
       `now & ~keys_prev` 0: prev is a plain static, used only here) */
    edge = now & ~prev;
    prev = now;
    keys_latched |= edge;
    for (k = 0; k < 4; k++)
        if (sfx_queue[k]) {
            const int8_t *t = sfx_tab[sfx_queue[k] - 1];
            ay_sfx(t[0], t[1] ? AY_NOTE(t[1]) : 0, t[2], t[3], t[4]);
            if (sfx_queue[k] == SFX_HURT) ay_sfx(0, AY_NOTE(40), 14, 1, 10);
            sfx_queue[k] = 0;
        }
    sound_tick();
}

int main(void)
{
    uint8_t acc = 0, loops = 0, in, edge, keys, blank = 0;
    uint16_t tstart;

    next_init();
    /* the ROM's font into page 39 (front.c makes the tiles from it there) */
    ZXN_WRITE_MMU3(39);
    memcpy((void *)0x6000, (const void *)0x3D00, 768);
    ZXN_WRITE_MMU3(11);
    ZXN_WRITE_REG(0x15, 0x13);                  /* tilemap over sprites over Layer 2; sprites over the border */
    frame_init();
    input_init();
    input_latch(1);
    ay_init();
    frame_hook = on_frame;
    l2_init();
    map_front();
    front_setup();                              /* the tilemap, the palettes, the copper */
    map_render();
    render_init();
    spr_setup();
    map_front();
    front_init();
    map_rules();
    cr_new_game(&game, 777, 1);
    zx_log("canyon: ready\n");
    tstart = frames50;
    for (;;) {
        acc += frame_wait();
        l2_flip();
        map_render();
        spr_present();
        in = input_read();
        edge = input_pressed();
        keys = keys_latched;
        keys_latched = 0;
        map_front();
        front_input(in, edge, keys);
        if (start_stage_req) {
            map_rules();
            cr_new_game(&game, next_seed, start_stage_req);
            next_seed++;                        /* (zsdcc: not as the argument, a 32-bit ++ there pushed garbage) */
            start_stage_req = 0;
            map_front();
            front_started();
            acc = 0;
        }
        if (rl.state == RS_TITLE && (game.over || game.won || game.between == 1)) {
            map_rules();                        /* the demo goes on with another ride */
            cr_new_game(&game, game.seed + 1, 1 + (int)(game.seed % CR_STAGES));
        }
        if (acc > 8) acc = 8;
        while (acc >= 2) {                      /* the original's 25 ticks a second */
            acc -= 2;
            if (rl.help || rl.paused) continue;
            map_rules();
            if (rl.state == RS_TITLE) cr_tick(&game, cr_bot(&game));
            else if (rl.state == RS_PLAY) cr_tick(&game, rl.autoplay ? cr_bot(&game) : play_in);
            map_front();
            front_after_tick();
        }
        map_front();
        if (front_scene()) {
            blank = 0;
            map_render();
            render_tracer = rl.tracer_timer > 0 ? (game.last_shot_hit ? PEN_YELLOW : PEN_GREY) : 0;
            render_scene(&game);
            spr_objects(&game);
        } else if (blank < 2) {                 /* the help pages, the initials: no view */
            blank++;
            l2_clear(PEN_PANEL);                /* (it puts the ROM back in slots 0-1) */
            map_render();
            spr_begin();
            spr_end();
        }
        map_front();
        front_draw();
        if (++loops == 50) {
            zx_log("canyon: fps*10=%u polys=%u\n", (uint16_t)(5000U * 5 / (uint16_t)(frames50 - tstart)),
                   render_polys);
            loops = 0;
            tstart = frames50;
        }
    }
}
#endif
