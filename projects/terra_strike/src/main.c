/*
 * Terra Strike for the ZX Spectrum Next - port of amiga_mcp/examples/terra_strike.
 *
 * world.c, terrain.c and ts_math.c are the original's simulation (port
 * changes marked port:), run at its fixed 25 Hz; render.c draws its 3D view
 * on Layer 2; this file is the original's game.c / main.c frame loop with
 * the Next front end: the HUD and radar (hud.c), sound (sound.c), the title,
 * the high scores.
 *
 * Keys, as the original: cursor keys 5 6 7 8 (or Kempston) turn, climb and
 * dive; A / Z throttle up / down; SPACE (or fire) fires; P pauses; ENTER
 * starts; BREAK goes back to the title; S turns the music off / on.
 *
 * Memory: the code runs in three phases, each with its own pages over the
 * ROM (whose font is copied first) at 0x0000-0x5FFF:
 *   simulation  0x0000 bank 22: world.c; 0x4000 page 46: world_setup.c
 *               (the original's simulation, ~20K of zsdcc's 32-bit code)
 *   3D view     0x0000 bank 24: render.c and the models; 0x4000 page 50: its
 *               draw list (nexttile maps the tilemap's page there for a moment
 *               when it writes)
 *   HUD         0x0000 bank 26: hud.c
 *   always      0x6000 page ISLAND_PAGE: the 64 x 64 heights (l2poly maps
 *               Layer 2 there while it fills); 0x8000-0xEFFF the program
 *               (terrain.c, ts_math.c: both phases use them), the SDK, the C
 *               library; 0xF000 the IM 2 table, the stack
 *   banks 9-14  Layer 2 256 x 192, double buffered
 */
#include <stdint.h>
#include <string.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nextcopper.h"
#include "ts.h"
#include "models.h"
#include "render.h"
#include "island.h"
#include "hud.h"
#include "sound.h"
#include "points.h"
#include <z80.h>

#define LOGIC_PAGE  44                  /* bank 22: world.c */
#define SETUP_PAGE  46                  /* world_setup.c */
#define RENDER_PAGE 48                  /* bank 24: render.c, the models */
#define DRAW_PAGE   50                  /* render.c's draw list */
#define HUD_PAGE    52                  /* bank 26: hud.c */

#define FONT_PAGE   54                  /* the ROM font's copy, at start-up (hud_init) */

/* the memory map's three phases (see the top of this file) */
static void map_logic(void)
{
    ZXN_WRITE_MMU0(LOGIC_PAGE);
    ZXN_WRITE_MMU1(LOGIC_PAGE + 1);
    ZXN_WRITE_MMU2(SETUP_PAGE);
}

static void map_render(void)
{
    ZXN_WRITE_MMU0(RENDER_PAGE);
    ZXN_WRITE_MMU1(RENDER_PAGE + 1);
    ZXN_WRITE_MMU2(DRAW_PAGE);
}

static void map_hud(void)
{
    ZXN_WRITE_MMU0(HUD_PAGE);
    ZXN_WRITE_MMU1(HUD_PAGE + 1);
}

World world;                            /* test.py reads it by symbol */
extern uint16_t clip_n, clip_kept;      /* render.c's (bank 24): its clip counts */
RenderStats render_stats;

/* phase timings in raster lines (312 a frame), summed over a perf period */
static uint32_t prof_t[10];
static uint16_t prof_last;
static volatile uint16_t isr_frames;    /* every interrupt */
static uint16_t raw_line(void)
{
    uint8_t hi = next_reg_read(0x1E), lo = next_reg_read(0x1F);
    return (uint16_t)(hi & 1) << 8 | lo;
}
uint16_t isr_line;
static uint16_t now_lines(void)
{
    uint16_t f, line;
    do {
        f = isr_frames;
        line = raw_line();
    } while (f != isr_frames);
    /* the line counted from the interrupt's */
    return f * 312 + (line >= isr_line ? line - isr_line : line + 312 - isr_line);
}
void prof(uint8_t phase)
{
    uint16_t t = now_lines();
    prof_t[phase] += (uint16_t)(t - prof_last);
    prof_last = t;
}

/* for the test: frames, simulation steps, screens drawn */
struct { uint16_t frames, steps, draws; } ts_perf;

/* ------------------------------------------------------------ the palette */

/* the original's 32 pens (main.c, 12-bit Amiga RGB) */
static const uint16_t pal12[32] = {
    0x000, 0xFFF, 0x333, 0x999,
    0x027, 0x04A,            /* sea */
    0xB95, 0xDB7,            /* sand */
    0x392, 0x5B3,            /* grass */
    0x161, 0x283,            /* forest */
    0x654, 0x987,            /* rock */
    0xAAB, 0xEEF,            /* snow */
    0xF30, 0x900, 0xFE0, 0xF80,
    0x9EF, 0x59B, 0x357,     /* craft */
    0xC64, 0x732,            /* tank */
    0xDDC, 0x998, 0xB43,     /* building, roof */
    0xABC, 0x567,            /* ship */
    0x2C3, 0x111             /* tree, shadow */
};

static uint16_t rgb9(uint16_t c)
{
    return ((c >> 9) & 7) << 6 | ((c >> 5) & 7) << 3 | ((c >> 1) & 7);
}

static void palettes(void)
{
    uint8_t i;
    for (i = 0; i < 32; i++) pal_set(1, i, rgb9(pal12[i]));
    /* sprites: palette offset k, pixel 0 -> entry 16 k (points.c) */
    for (i = 0; i < 16; i++) pal_set(2, 16 * i, rgb9(pal12[points_pen_of_offset(i)]));
}

/* ------------------------------------------------------------ the copper: rows twice */

/* port: the 3D view is drawn 85 rows high and shown 170: every other line
   the copper moves Layer 2's Y scroll back a row (as Planet Chomp) */
static void copper_doubling(void)
{
    uint16_t l;
    copper_begin();
    copper_move(0x17, 0);
    for (l = 2; l < R_H; l += 2) {
        copper_wait(l - 1, 50);
        copper_move(0x17, (uint8_t)(192 - l / 2));
    }
    copper_wait(R_H - 1, 50);
    copper_move(0x17, 0);
    copper_end();
}

/* ------------------------------------------------------------ input (the original's keys) */

static uint16_t read_input(void)
{
    uint16_t in = 0;
    uint8_t k = input_kempston ? z80_inp(0x1F) : 0;
    if ((k & 0xE0) == 0) {          /* bits 5-7 set: no Kempston there (CLAUDE.md) */
        if (k & 0x02) in |= TSI_LEFT;
        if (k & 0x01) in |= TSI_RIGHT;
        if (k & 0x08) in |= TSI_UP;
        if (k & 0x04) in |= TSI_DOWN;
        if (k & 0x10) in |= TSI_FIRE;
    }
    if (key_down(IN_KEY_SCANCODE_5)) in |= TSI_LEFT;
    if (key_down(IN_KEY_SCANCODE_8)) in |= TSI_RIGHT;
    if (key_down(IN_KEY_SCANCODE_7)) in |= TSI_UP;
    if (key_down(IN_KEY_SCANCODE_6)) in |= TSI_DOWN;
    if (key_down(IN_KEY_SCANCODE_a)) in |= TSI_FAST;
    if (key_down(IN_KEY_SCANCODE_z)) in |= TSI_SLOW;
    if (key_down(IN_KEY_SCANCODE_SPACE) || key_down(IN_KEY_SCANCODE_0)) in |= TSI_FIRE;
    if (key_down(IN_KEY_SCANCODE_ENTER)) in |= TSI_START;
    if (key_down(IN_KEY_SCANCODE_p)) in |= TSI_PAUSE;
    {   /* port: a tap shorter than a slow frame (5 fps) is latched by the
           interrupt (input_sample); the keys above are only seen if held */
        uint8_t l;
        input_read();
        l = input_pressed();
        if (l & IN_START) in |= TSI_START;
        if (l & IN_FIRE) in |= TSI_FIRE;
    }
    if (key_down(IN_KEY_SCANCODE_CAPS) && key_down(IN_KEY_SCANCODE_SPACE)) in = (in & ~TSI_FIRE) | TSI_ESC;
    return in;
}

/* ------------------------------------------------------------ events: sound and log */

/* the effects wait for the HUD phase (sound.c runs from the HUD's bank) */
static uint8_t snd_q[8], snd_qn;

static void handle_events(void)
{
    Event e;
    while (world_pop_event(&world, &e)) {
        if (snd_qn < sizeof(snd_q)) snd_q[snd_qn++] = (uint8_t)e.type;
        switch (e.type) {
        case EV_TAKEOFF: zx_log("ts: TAKEOFF fuel=%d\n", e.a); break;
        case EV_LANDED:  zx_log("ts: LANDED fuel=%d\n", e.a); break;
        case EV_KILL:    zx_log("ts: KILL %s score=%ld targets_left=%d\n", ent_name(e.a), (long)world.score, e.b); break;
        case EV_HIT_PLAYER: zx_log("ts: HIT shield=%d\n", e.a); break;
        case EV_CRASH:   zx_log("ts: CRASH lives=%d\n", e.a); break;
        case EV_RESPAWN: zx_log("ts: RESPAWN lives=%d\n", e.a); break;
        case EV_LEVEL_CLEAR: zx_log("ts: LEVEL %d CLEAR score=%ld\n", e.a, (long)world.score); break;
        case EV_GAME_OVER: zx_log("ts: GAME OVER score=%ld level=%d\n", (long)world.score, world.level); break;
        case EV_LOW_FUEL: zx_log("ts: LOW FUEL\n"); break;
        }
    }
}

/* ------------------------------------------------------------ main */

static void isr_work(void)
{
    isr_frames++;
    isr_line = raw_line();
    input_sample();
}

int main(void)
{
    uint8_t acc = 0, paused = 0, last_state = 255;
    uint8_t music_key = 0, music_toggle = 0;
    uint16_t prev_in = 0, perf_frames = 0, perf_draws = 0, perf_steps = 0;

    next_init();
    ZXN_WRITE_MMU3(FONT_PAGE);          /* the ROM font to 0x6000 before the ROM is paged out */
    memcpy((void *)0x6000, (const void *)0x3D00, 512);
    map_hud();
    hud_init();
    map_logic();
    ZXN_WRITE_MMU3(ISLAND_PAGE);
    l2_init();
    points_init();
    palettes();
    map_hud();
    snd_init();
    snd_music(1);                       /* the original's MOD, all the time */
    map_logic();
    input_init();
    frame_init();
    frame_hook = isr_work;
    input_latch(1);
    hs_init(1000);                      /* port: the original keeps no table */
    map_render();
    render_init();
    map_logic();
    world_init(&world, ISLAND_SEED);
    copper_doubling();
    ZXN_WRITE_REG(0x15, 0x0B);          /* sprites on, over the border; layers S U L: the radar's dots over the HUD */
    zx_log("ts: ready kempston=%u turbo=%u pad=%d,%d\n", input_kempston, next_reg_read(7) & 3,
           world.pad_tx, world.pad_tz);

    for (;;) {
        uint8_t ticks, steps;
        uint16_t in;

        ticks = frame_wait();
        l2_flip();                      /* the frame drawn last time */
        points_show();
        perf_frames += ticks;
        ts_perf.frames += ticks;

        /* the original's game_input / game_update: a fixed 25 Hz simulation */
        prof(0);                        /* waiting, flip, sprites, sound */
        in = read_input();
        if ((in & ~prev_in) & TSI_PAUSE) paused = !paused;
        if (((in & ~prev_in) & TSI_ESC) && world.state != GS_TITLE) {
            world.state = GS_TITLE;
            zx_log("ts: back to title\n");
        }
        prev_in = in;
        {   /* S: the music off / on (port: the original has no key for it) */
            uint8_t m = key_down(IN_KEY_SCANCODE_s);
            if (m && !music_key) music_toggle = 1;
            music_key = m;
        }
        acc += ticks;
        steps = acc >> 1;
        acc &= 1;
        if (steps > 4) steps = 4;
        while (steps-- && !paused) {
            world_update(&world, in);
            handle_events();
            ts_perf.steps++;
            perf_steps++;
        }
        if (world.state != last_state) {
            static const char *const names[4] = { "TITLE", "PLAY", "LEVELCLEAR", "GAMEOVER" };
            zx_log("ts: STATE %s level %d score=%ld\n", names[world.state], world.level, (long)world.score);
            last_state = (uint8_t)world.state;
        }

        prof(1);                        /* the simulation */
        map_render();
        camera_follow(&cam, &world);
        render_build(&world);
        prof(2);
        render_draw();
        prof(3);
        map_hud();
        {
            uint8_t q;
            if (music_toggle) {
                snd_music(!snd_music_on());
                zx_log("ts: music %u\n", snd_music_on());
                music_toggle = 0;
            }
            for (q = 0; q < snd_qn; q++) snd_event(snd_q[q]);
            snd_qn = 0;
            snd_tick(ticks);
        }
        hud_draw(&world, paused);
        map_logic();
        prof(4);
        ts_perf.draws++;
        perf_draws++;
        if (perf_frames >= 250) {
            zx_log("ts: perf fps10=%u steps=%u polys=%u tiles=%u models=%u dropped=%u\n",
                   (uint16_t)(perf_draws * 500 / perf_frames), perf_steps, render_stats.polys,
                   render_stats.tiles, render_stats.models, render_stats.dropped);
            {   /* clip_n, clip_kept are render.c's (bank 24): map it to read
                   and reset them (with the logic's bank mapped this wrote
                   into world.c's code: aim_target) */
                uint16_t cn, ck;
                map_render();
                cn = clip_n; ck = clip_kept;
                clip_n = clip_kept = 0;
                map_logic();
                zx_log("ts: mdl setup=%u xf=%u faces=%u clip=%u kept=%u\n", (uint16_t)(prof_t[8] / perf_draws), (uint16_t)(prof_t[9] / perf_draws), (uint16_t)(prof_t[4] / perf_draws), cn / perf_draws, ck / perf_draws);
            }
            zx_log("ts: build verts=%u tiles=%u models=%u points=%u\n",
                   (uint16_t)(prof_t[5] / perf_draws), (uint16_t)(prof_t[6] / perf_draws),
                   (uint16_t)(prof_t[7] / perf_draws), (uint16_t)(prof_t[2] / perf_draws));
            zx_log("ts: lines/draw wait=%u sim=%u build=%u fill=%u hud=%u\n",
                   (uint16_t)(prof_t[0] / perf_draws), (uint16_t)(prof_t[1] / perf_draws),
                   (uint16_t)(prof_t[2] / perf_draws), (uint16_t)(prof_t[3] / perf_draws),
                   (uint16_t)(prof_t[4] / perf_draws));
            memset(prof_t, 0, sizeof(prof_t));
            perf_frames = perf_draws = perf_steps = 0;
        }
    }
}
