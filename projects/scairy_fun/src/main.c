/*
 * ScAIry Fun for the Spectrum Next: memory map, start-up and the frame loop.
 *
 *   0x0000-0x3FFF  bank 16: world_traffic.c, world_scene.c  } map_sim(): the
 *   0x4000-0x7FFF  bank 17: world_ai.c, route.c              } simulation step
 *   0x0000-0x3FFF  bank 18: the renderer and front end       } map_render()
 *   0x4000-0x5FFF  page 38: the renderer's variables         }
 *   0x6000-0x7FFF  Layer 2 pages while drawing (l2poly), bank 5 for the tilemap
 *   0x8000-0xEFFF  resident: world.c, track.c, the SDK, the C library, data
 *   0xF000-        the IM 2 table, the stack
 */
#ifndef SC_TRACE
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include <input.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "sc.h"
#include "render.h"
#include "sprites.h"
#include "game.h"
#include "modplay.h"
#include "palfx.h"
uint8_t lim_slope = 99, lim_wall = 99, lim_scene = 99, lim_draw = 99;   /* EXPERIMENT */
uint8_t render_skip;                    /* profiling (render.c) */
extern const mod_song scairy_song;      /* music.c (in page 39 for 0xF200, see main) */

World world;
uint8_t sfx_queue;

static volatile uint16_t frames50;               /* 50 Hz frames, from the interrupt */

void map_sim(void)
{
    ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33);
    ZXN_WRITE_MMU2(34); ZXN_WRITE_MMU3(35);
}

void map_front(void)
{
    ZXN_WRITE_MMU0(40); ZXN_WRITE_MMU1(41);
}

void map_render(void)
{
    ZXN_WRITE_MMU0(36); ZXN_WRITE_MMU1(37);
    ZXN_WRITE_MMU2(38); ZXN_WRITE_MMU3(11);
}

/* channel, MIDI note (0: noise), volume, decay, sweep; SFX_* order (game.h) */
static const int8_t sfx_tab[][5] = {
    { 0, 72, 13, 1, -3 },   /* chime on */
    { 0, 67, 13, 1, 4 },    /* chime off */
    { 1, 84, 11, 2, 0 },    /* blip */
    { 0, 76, 15, 1, -6 },   /* save */
    { 2, 0, 12, 2, 0 },     /* scrape */
    { 1, 88, 12, 3, 8 },    /* cone */
    { 2, 0, 15, 1, 0 },     /* crash */
    { 2, 0, 14, 1, 0 },     /* splash */
    { 1, 81, 14, 1, 0 },    /* alert */
    { 1, 79, 10, 1, 0 },    /* siren (74 every other time) */
    { 1, 79, 10, 3, 0 },    /* key */
};

static void start_sfx(uint8_t id)
{
    const int8_t *t = sfx_tab[id - 1];
    uint8_t note = t[1];
    if (id == SFX_SIREN && (frames50 & 8)) note = 74;
    ay_sfx(t[0], note ? AY_NOTE(note) : 0, t[2], t[3], t[4]);
    if (id == SFX_CRASH) ay_sfx(0, AY_NOTE(40), 14, 1, 12);
}

uint8_t keys_latched;                           /* V, H, S seen since game_input looked */

static void on_frame(void)
{
    frames50++;
    input_sample();
    if (key_down(IN_KEY_SCANCODE_v)) keys_latched |= 1;
    if (key_down(IN_KEY_SCANCODE_h)) keys_latched |= 2;
    if (key_down(IN_KEY_SCANCODE_s)) keys_latched |= 4;
    modplay_tick();
    if (sfx_queue) {
        start_sfx(sfx_queue);
        sfx_queue = 0;
    }
    sound_tick();
}

/* the simulation steps due: fixed 25 Hz (the original's), at most 4 at a time */
static uint8_t acc;

int main(void)
{
    uint8_t t, fr;
    unsigned in;
    uint16_t loops = 0, tstart;

    next_init();
    ZXN_WRITE_REG(0x15, 0x13);                      /* tilemap over sprites over Layer 2 */
    /* the music's data, linked at 0xF200 and loaded into page 39: copy it up there */
    ZXN_WRITE_MMU3(39);
    memcpy((void *)0xF200, (const void *)0x7200, 0xFE00 - 0xF200);
    ZXN_WRITE_MMU3(11);
    /* the ROM's font for the tilemap (ui_font), into page 39's first 768 bytes */
    ZXN_WRITE_MMU3(39);
    memcpy((void *)0x6000, (const void *)0x3D00, 768);
    ZXN_WRITE_MMU3(11);
    frame_init();
    input_init();
    input_latch(1);
    ay_init();
    frame_hook = on_frame;
    l2_init();
    spr_init();
    map_render();
    render_init();
    spr_setup();
    ZXN_WRITE_MMU3(42);                             /* copper.c: set_palette */
    set_palette(0, 0);
    ZXN_WRITE_MMU3(11);
    map_front();
    ui_font();
    game_init();
    map_sim();
    world_init(&world);
    modplay_start(&scairy_song);
    zx_log("scairy: ready\n");
    tstart = frames50;
    for (;;) {
        static uint8_t pal_route = 0xFF, pal_glare = 0xFF, pal_view = 0xFF;
        t = frame_wait();
        l2_flip();
        map_render();
        spr_show_frame();
        if (world.route != pal_route || world.glare != pal_glare) {
            ZXN_WRITE_MMU3(42);                     /* copper.c */
            set_palette(world.route, world.glare);  /* (in the vertical blank) */
            ZXN_WRITE_MMU3(11);
            if (world.route != pal_route) pal_view = 0xFF;
            pal_route = world.route;
            pal_glare = world.glare;
        }
        if (world.view != pal_view) {
            ZXN_WRITE_MMU3(42);                     /* copper.c */
            render_copper(world.route, world.view);
            ZXN_WRITE_MMU3(11);
            pal_view = world.view;
        }
        map_front();
        fr = game_input();
        in = game_in;                               /* (game.c's variables are in bank 20) */
        if (game_reset) {
            game_reset = 0;
            map_sim();
            world_init(&world);
            acc = 0;
        }
        acc += t;
        if (acc > 8) acc = 8;                       /* the original's: at most 4 steps a frame */
        if (fr & FR_PLAY) {
            map_sim();
            while (acc >= 2 && !(render_skip & 128)) {
                world_update(&world, in);
                acc -= 2;
                map_front();
                game_events();                      /* sounds, the log */
                map_sim();
            }
        } else acc = 0;
        if (fr & FR_3D) {
            map_render();
            camera_follow(&world);
            render_world(&world);
        } else {
            map_render();
            spr_hide_all();
        }
        map_front();
        game_draw();
        if (++loops == 50) {
            uint16_t f = frames50 - tstart;
            zx_log("scairy: fps*10=%u seg=%d\n", (uint16_t)(5000UL * 50 / 10 / f), seg_of(world.car.z));
            loops = 0;
            tstart = frames50;
        }
    }
}
#endif
