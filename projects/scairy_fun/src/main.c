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
uint8_t render_skip;                    /* profiling (render.c) */

World world;

/* time in raster lines (312 a frame) since `from`, for profiling */
static uint32_t now_lines(void)
{
    uint8_t f = frame_ticks;
    uint16_t l = ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);
    if (f != frame_ticks) return now_lines();
    return (uint32_t)f * 312 + l;
}
static uint16_t lines_since(uint32_t t0)
{
    int32_t d = (int32_t)(now_lines() - t0);
    if (d < 0) d += 256L * 312;
    return (uint16_t)d;
}
static uint32_t t_sim, t_draw;
static volatile uint16_t frames50;               /* 50 Hz frames, from the interrupt */
static void on_frame(void) { frames50++; }

void map_sim(void)
{
    ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33);
    ZXN_WRITE_MMU2(34); ZXN_WRITE_MMU3(35);
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
            pal_set(1, f * 32 + i, rgb9(c));
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

int main(void)
{
    uint8_t acc = 0, t;
    uint16_t frames = 0, fstart;
    uint16_t tstart = frames50;
    next_init();
    frame_init();
    frame_hook = on_frame;
    l2_init();
    spr_init();
    map_render();
    render_init();
    spr_setup();
    set_palette(0, 0);                  /* the tables are in the renderer's bank */
    map_sim();
    world_init(&world);
    zx_log("scairy: ready\n");
    fstart = frame_count;
    for (;;) {
        t = frame_wait();
        l2_flip();
        map_render();
        spr_show_frame();
        acc += t;
        if (acc > 8) acc = 8;           /* the original's: at most 4 steps a frame */
        map_sim();
        {
        uint32_t t0 = now_lines();
        while (acc >= 2 && !(render_skip & 128)) {
            Event e;
            world_update(&world, 0);
            while (world_pop_event(&world, &e)) ;
            acc -= 2;
        }
        t_sim += lines_since(t0);
        t0 = now_lines();
        map_render();
        camera_follow(&world);
        render_world(&world);
        t_draw += lines_since(t0);
        }
        if (++frames == 20) {
            zx_log("scairy: fps*10=%u polys=%u seg=%d sim=%u draw=%u lines/frame\n",
                   (uint16_t)(10000U / (frames50 - tstart)), render_polys, seg_of(world.car.z),
                   (uint16_t)(t_sim / 20), (uint16_t)(t_draw / 20));
            t_sim = t_draw = 0;
            tstart = frames50;
            frames = 0;
            fstart = frame_count;
        }
    }
}
