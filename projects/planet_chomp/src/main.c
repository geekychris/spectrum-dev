/*
 * Planet Chomp for the Spectrum Next: memory map, start-up, input and the
 * loop (the original's main_68k.c).
 *
 *                  while the logic runs        while drawing
 *   0x0000-0x3FFF  bank 16: game.c, maze.c      bank 18: render.c, rtab.c, the draw list
 *   0x4000-0x5FFF  bank 17: the grid tables,    page 38: the corners
 *   0x6000-0x7FFF    the autopilot's arrays     page 39 (corner tables), page 40 (wall
 *                                               tables), then Layer 2 (l2poly)
 *   0x8000-0xEFFF  this, the SDK, the shared state (state.c); 0xF000- the IM 2 table, the stack
 *   page 41 at 0x6000 after drawing: view.c (the sprites' positions, the radar);
 *   bank 21 at 0x0000 for the HUD: hud.c (the tilemap in bank 5 through slot 2)
 *   banks 9-14     Layer 2 (two 256 x 192 buffers); 22-24 the starfield;
 *   26-35 the sprites (actors.c)
 *
 * port: zsdcc's code for the original's rules is about 15K and the grid's
 * tables 11K, so the program is in parts that the loop maps in turn. Code
 * in one bank only calls main memory, never another bank.
 *
 * Each frame: the frame interrupt, then Layer 2 flips to the frame drawn
 * last time, the logic runs once per frame that passed (fixed timestep, at
 * most 4), the starfield is copied into the back buffer by DMA and the
 * planet drawn over it.
 */
#include <stdint.h>
#include <stdio.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "l2poly.h"
#include "zxlog.h"
#include "pc.h"
#include "game.h"
#include "render.h"
#include "rtab.h"
#include "actors.h"
#include "hud.h"
#include "sound.h"
#include <input.h>
#include <string.h>

#define LOGIC() do { ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33); ZXN_WRITE_MMU2(34); ZXN_WRITE_MMU3(35); } while (0)
#define DRAW()  do { ZXN_WRITE_MMU0(36); ZXN_WRITE_MMU1(37); ZXN_WRITE_MMU2(38); } while (0)
#define HUD()   do { ZXN_WRITE_MMU0(42); ZXN_WRITE_MMU1(43); } while (0)
#define STARS_BANK 22

extern const uint16_t rd_pal[C_COUNT];
extern void bench_loop(void);
extern void bench_store(void);
int16_t rd_test[16];
extern void rd_init(void);
extern void rd_quad(const int16_t *xy, uint8_t colour);

void rd_map_corners(void) { ZXN_WRITE_MMU3(39); }
void rd_map_walls(void) { ZXN_WRITE_MMU3(40); }
void dr_begin(void) { l2p_begin(); }
void dr_end(void) { l2p_end(); }
void dr_poly(const int16_t *xy, uint8_t n, uint8_t colour) { l2p_poly(xy, n, colour); }
void dr_rect(int16_t y0, int16_t y1, int16_t x0, int16_t x1, uint8_t colour) { l2p_rect(y0, y1, x0, x1, colour); }


/* profiling: raster lines spent in each part of the loop (one line is ~1800
   T-states at 28 MHz), on a clock of frames (the interrupt) and lines */
static volatile uint16_t irq_frames;
static void on_frame(void) { irq_frames++; input_sample(); }
static uint32_t line_clock(void)
{
    uint16_t f, r;
    do {
        f = irq_frames;
        r = ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F);
    } while (f != irq_frames);
    /* the interrupt comes at line 248 (the frame's start): lines since then */
    return (uint32_t)f * 312 + (r >= 248 ? r - 248 : r + 64);
}
static uint32_t prof_t, prof[4], rprof[5];
static uint16_t perf_ticks;
void rd_lap(uint8_t k)
{
    uint32_t t = line_clock();
    rprof[k] += t - prof_t;
    prof_t = t;
}
static void lap(uint8_t k)
{
    uint32_t t = line_clock();
    prof[k] += t - prof_t;
    prof_t = t;
}

/* port: the original's Q/E (spin) are taken by QAOP: Z and X spin; P is
   right, so pause is H; C is the whole planet, as in the original */
static uint16_t extra_keys(void)
{
    uint16_t b = 0;
    if (key_down(IN_KEY_SCANCODE_z)) b |= PAD_L;
    if (key_down(IN_KEY_SCANCODE_x)) b |= PAD_R;
    if (key_down(IN_KEY_SCANCODE_c)) b |= PAD_C;
    if (key_down(IN_KEY_SCANCODE_h)) b |= PAD_P;
    if (key_down(IN_KEY_SCANCODE_b)) b |= 0x8000;         /* B: the bot (tests) */
    return b;
}

/* keys and Kempston -> the original's pad bits */
static uint16_t pad_of(uint8_t in)
{
    uint16_t b = 0;
    if (in & IN_UP) b |= PAD_UP;
    if (in & IN_DOWN) b |= PAD_DOWN;
    if (in & IN_LEFT) b |= PAD_LEFT;
    if (in & IN_RIGHT) b |= PAD_RIGHT;
    if (in & (IN_FIRE | IN_START)) b |= PAD_A;
    return b;
}

int main(void)
{
    uint8_t ticks, i, frames = 0, last_state = 0xFF, entering = 0;
    int8_t place = -1;
    uint16_t held, pressed, fps_t0, extra, extra_last = 0;
    hs_editor ed;

    memcpy((void *)0x7000, (const void *)0x3D00, 768);   /* the ROM font into bank 5 (for the HUD) */
    next_init();
    zx_border(INK_BLACK);
    act_init();
    ZXN_WRITE_REG(0x15, 0x13);                       /* sprites on, over the border; order U S L */
    l2_init();
    pal_load(1, rd_pal, C_COUNT);
    input_init();
    frame_init();
    frame_hook = on_frame;
    input_latch(1);
    DRAW();
    rd_init();
    HUD();
    hud_init();
    hs_init(1000);
    snd_init();
    LOGIC();
    game_init();
    g.hiscore = (int32_t)hs_table[0].score;
    zx_log("pc: ready kempston=%u walls=%d\n", input_kempston, rd_nwalls);
    fps_t0 = irq_frames;

    for (;;) {
        ticks = frame_wait();
        prof_t = line_clock();
        l2_flip();
        act_commit();
        lap(3);
        if (ticks > 8) ticks = 8;                    /* the game's speed holds down to 6 fps */
        perf_ticks += ticks;
        LOGIC();
        if (entering) {                              /* the initials: the keys are the editor's */
            uint8_t r;
            input_read();
            r = hs_edit_step(&ed);
            held = pressed = 0;
            if (r & HS_EDIT_DONE) {
                hs_insert(place, ed.name, (uint32_t)g.score);
                g.hiscore = (int32_t)hs_table[0].score;
                zx_log("pc: name=%s\n", ed.name);
                game_title();
                entering = 0;
            }
        } else {
            held = pad_of(input_read());
            pressed = pad_of(input_pressed());
            extra = extra_keys();
            held |= extra;
            pressed |= extra & ~extra_last;
            extra_last = extra;
            if (pressed & 0x8000) {                  /* port: B lets the autopilot play (the test's bot) */
                pc_bot = !pc_bot;
                zx_log("pc: bot=%u\n", pc_bot);
            }
            held &= 0x7FFF;
            pressed &= 0x7FFF;
        }
        for (i = 0; i < ticks; i++) {
            game_step(held, pressed);
            pressed = 0;                             /* an edge counts once */
            snd_tick();
        }
        snd_title(g.state == GS_TITLE);
        if (g.state != last_state) {
            if (g.state == GS_OVER && !g.demo) {
                place = hs_rank((uint32_t)g.score);
                zx_log("pc: gameover score=%ld rank=%d\n", g.score, place);
                if (place >= 0) {
                    hs_edit_begin(&ed);
                    entering = 1;
                }
            }
            if (g.state == GS_TITLE) zx_log("pc: title\n");
            last_state = g.state;
        }
        bot_update();
        game_view();
        lap(0);
        l2_copy_to_back(STARS_BANK);
        lap(1);
        DRAW();
        if (rd_dbg & 16) {                           /* test: quads from rd_test (set by test.py) */
            l2_clear(C_SPACE);
            DRAW();                                  /* (the DMA gave slots 0-1 to the ROM) */
            l2p_begin();
            rd_quad(rd_test, C_WALL_SIDE);
            rd_quad(rd_test + 8, C_CRUMB);
            l2p_end();
        } else if (rd_dbg & 8) {                            /* benchmark: 200 quads 4 x 20, then 200 quads 20 x 4 */
            static int16_t q[8];
            uint16_t n;
            uint32_t t0;
            l2p_begin();
            t0 = line_clock();
            for (n = 0; n < 200; n++) {
                q[0] = n & 127; q[1] = 20 + (n & 63); q[2] = q[0] + 4; q[3] = q[1] + 1;
                q[4] = q[2] + 1; q[5] = q[1] + 21; q[6] = q[0] + 1; q[7] = q[1] + 20;
                l2p_poly(q, 4, 9);
            }
            t0 = line_clock() - t0;
            zx_log("pc: bench tall quads: %lu lines\n", t0);
            t0 = line_clock();
            for (n = 0; n < 200; n++) {
                q[0] = n & 127; q[1] = 20 + (n & 63); q[2] = q[0] + 20; q[3] = q[1] + 1;
                q[4] = q[2] + 1; q[5] = q[1] + 5; q[6] = q[0] + 1; q[7] = q[1] + 4;
                l2p_poly(q, 4, 9);
            }
            t0 = line_clock() - t0;
            zx_log("pc: bench wide quads: %lu lines\n", t0);
            t0 = line_clock();
            for (n = 0; n < 200; n++) {
                q[0] = n & 127; q[1] = 20 + (n & 63); q[2] = q[0] + 4; q[3] = q[1] + 1;
                q[4] = q[2] + 1; q[5] = q[1] + 21; q[6] = q[0] + 1; q[7] = q[1] + 20;
                rd_quad(q, 9);
            }
            t0 = line_clock() - t0;
            zx_log("pc: bench rd_quad tall: %lu lines\n", t0);
            t0 = line_clock();
            for (n = 0; n < 200; n++) {
                q[0] = n & 127; q[1] = 20 + (n & 63); q[2] = q[0] + 20; q[3] = q[1] + 1;
                q[4] = q[2] + 1; q[5] = q[1] + 5; q[6] = q[0] + 1; q[7] = q[1] + 4;
                rd_quad(q, 9);
            }
            t0 = line_clock() - t0;
            zx_log("pc: bench rd_quad wide: %lu lines\n", t0);
            t0 = line_clock();
            for (n = 0; n < 200; n++) l2p_rect(20 + (n & 63), 23 + (n & 63), n & 127, (n & 127) + 2, 7);
            t0 = line_clock() - t0;
            zx_log("pc: bench rects: %lu lines\n", t0);
            {
                t0 = line_clock();
                bench_loop();
                t0 = line_clock() - t0;
                zx_log("pc: bench 65536 x 26T loop: %lu lines\n", t0);
                ZXN_WRITE_MMU3(l2_back * 2);
                t0 = line_clock();
                bench_store();
                t0 = line_clock() - t0;
                zx_log("pc: bench 65536 L2 stores (11T + 1.6T loop): %lu lines\n", t0);
            }
            l2p_end();
        } else {
            rd_frame();
            ZXN_WRITE_MMU3(41);
            rd_sprites();
        }
        lap(2);
        HUD();
        if (entering) hud_entry(ed.name, ed.pos, (uint8_t)place);
        else hud_update();
        if ((frames & 7) == 7)                       /* the test checks the music and effects play */
            zx_log("pc: ay vol0=%u vol1=%u vol2=%u\n", snd_reg(0, 8) & 15, snd_reg(1, 8) & 15, snd_reg(2, 8) & 15);
        if (++frames == 50) {
            uint16_t f = irq_frames - fps_t0;
            zx_log("pc: perf fps10=%u lines/frame: commit=%lu logic=%lu stars=%lu view=%lu walls=%d crumbs=%d ticks=%u\n",
                   (uint16_t)(500UL * 50 / f), prof[3] / 50, prof[0] / 50, prof[1] / 50, prof[2] / 50, rd_stats_walls,
                   rd_stats_crumbs, perf_ticks);
            perf_ticks = 0;
            zx_log("pc: perf draw lines/frame: camera=%lu corners=%lu crumbs=%lu walls=%lu fill=%lu\n",
                   rprof[0] / 50, rprof[1] / 50, rprof[2] / 50, rprof[3] / 50, rprof[4] / 50);
            prof[0] = prof[1] = prof[2] = prof[3] = 0;
            rprof[0] = rprof[1] = rprof[2] = rprof[3] = rprof[4] = 0;
            frames = 0;
            fps_t0 = irq_frames;
        }
    }
}
