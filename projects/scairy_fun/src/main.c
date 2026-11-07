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
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "sc.h"
#include "render.h"
#include "sprites.h"
#include "game.h"
#include "modplay.h"
#include "palfx.h"
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

static uint16_t rgb9(uint16_t c12)             /* 0xRGB (4 bits each) -> RGB333 */
{
    uint16_t r = (((c12 >> 8) & 15) * 7 + 7) / 15, g = (((c12 >> 4) & 15) * 7 + 7) / 15,
             b = ((c12 & 15) * 7 + 7) / 15;
    return r << 6 | g << 3 | b;
}

static uint16_t mix12(uint16_t a, uint16_t b, uint16_t num, uint16_t den)
{
    uint16_t r = ((a >> 8) & 15) * (den - num) + ((b >> 8) & 15) * num,
             g = ((a >> 4) & 15) * (den - num) + ((b >> 4) & 15) * num,
             c = (a & 15) * (den - num) + (b & 15) * num;
    return (r / den) << 8 | (g / den) << 4 | (c / den);
}

static uint16_t glared(uint16_t c, int glare)
{
    return glare > 0 ? mix12(c, 0xFFE, glare * 10, 16 * 16) : c;
}

/* the sprites' 16-colour layout (tools/gen_sprites.py): body lit, shaded, then
   glass, shadow, white x2, red x2, yellow, orange, brown x2, rock, dirt, light */
static const uint8_t spr_pens[13] = { 28, 31, 24, 25, 16, 17, 18, 19, 29, 30, 15, 13, 16 };
static const uint8_t spr_body[8][2] = {
    { 22, 23 }, { 24, 25 }, { 18, 19 }, { 26, 29 }, { 16, 17 }, { 3, 2 }, { 24, 25 }, { 24, 25 }
};

/* (call with the renderer's bank mapped: route_palette is there)
   Layer 2: the route's 32 pens, and three copies fading into the haze (port:
   256 colours); the sprites: a 16-colour bank for each body colour */
void set_palette(int route, int glare)
{
    const uint16_t *p = route_palette[route];
    uint8_t f, i;
    for (f = 0; f < 4; f++)
        for (i = 0; i < 32; i++) {
            uint16_t c = p[i];
            if (f && i != PEN_SKY) c = mix12(c, p[PEN_HAZE], f * 2, 8);
            if (i >= 2) c = glared(c, glare);
            pal_set(1, f * 32 + i, i == PEN_SKY ? 0xE3 << 1 | 1 : rgb9(c));    /* the sky: see render_copper */
        }
    for (f = 0; f < 8; f++) {
        pal_set(2, f * 16 + 1, rgb9(glared(p[spr_body[f][0]], glare)));
        pal_set(2, f * 16 + 2, rgb9(glared(p[spr_body[f][1]], glare)));
        for (i = 0; i < 13; i++) {
            uint8_t pen = i == 12 && f == 7 ? 22 : spr_pens[i];      /* the ambulance's blue */
            pal_set(2, f * 16 + 3 + i, rgb9(glared(p[pen], glare)));
        }
    }
}


/* the effects (game.c sfx(): queued, started here so only the interrupt writes
   to the AY), on chip 0; the music on chips 1 and 2 */
static void start_sfx(uint8_t id)
{
    switch (id) {
    case SFX_CHIME_ON:  ay_sfx(0, AY_NOTE(72), 13, 1, -3); break;
    case SFX_CHIME_OFF: ay_sfx(0, AY_NOTE(67), 13, 1, 4); break;
    case SFX_BLIP:      ay_sfx(1, AY_NOTE(84), 11, 2, 0); break;
    case SFX_SAVE:      ay_sfx(0, AY_NOTE(76), 15, 1, -6); break;
    case SFX_SCRAPE:    ay_sfx(2, 0, 12, 2, 0); break;
    case SFX_CONE:      ay_sfx(1, AY_NOTE(88), 12, 3, 8); break;
    case SFX_CRASH:     ay_sfx(2, 0, 15, 1, 0); ay_sfx(0, AY_NOTE(40), 14, 1, 12); break;
    case SFX_SPLASH:    ay_sfx(2, 0, 14, 1, 0); break;
    case SFX_ALERT:     ay_sfx(1, AY_NOTE(81), 14, 1, 0); break;
    case SFX_SIREN:     ay_sfx(1, AY_NOTE((frames50 >> 3) & 1 ? 79 : 74), 10, 1, 0); break;
    case SFX_KEY:       ay_sfx(1, AY_NOTE(79), 10, 3, 0); break;
    }
}

static void on_frame(void)
{
    frames50++;
    input_sample();
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
    set_palette(0, 0);
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
            set_palette(world.route, world.glare);  /* (in the vertical blank) */
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
