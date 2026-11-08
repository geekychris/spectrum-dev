/*
 * Lander 3D for the Spectrum Next: memory map, start-up, the 50 Hz loop.
 *
 *   0x0000-0x3FFF  bank 20: the rules (ld_core.c), or bank 22 while the
 *                  view works out the picture (view.c); Layer 2's back
 *                  buffer (with 0x4000-0x5FFF) while vfill draws it
 *   0x4000-0x5FFF  bank 5's first page (the tilemap); a level's heights
 *                  while the rules read them (ter.c)
 *   0x6000-0x7FFF  page 46 (the autopilot) with the rules, page 47 (vfill)
 *                  with the view
 *   0x8000-0xEFFF  the shell, the SDK, the variables; 0xF000- the IM 2
 *                  table, the stack
 *   banks 9-14     Layer 2 (two screens); 40-90 the levels (lv_rec.h)
 */
#include <string.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nextcopper.h"
#include "zxlog.h"
#include "ld.h"
#include "view.h"
#include "vfill.h"
#include "pens.h"
#include "flight_hash.h"

#define LOGIC() do { ZXN_WRITE_MMU0(40); ZXN_WRITE_MMU1(41); ZXN_WRITE_MMU3(46); } while (0)
#define VIEW()  do { ZXN_WRITE_MMU0(44); ZXN_WRITE_MMU1(45); ZXN_WRITE_MMU3(47); } while (0)

Level level;
static volatile uint16_t ticks;
static void tick_isr(void) { ticks++; }

static uint16_t rgb9(uint16_t c)        /* RGB444 -> RGB333 */
{
    return (uint16_t)(((c >> 9) & 7) << 6 | ((c >> 5) & 7) << 3 | ((c >> 1) & 7));
}

/* port: the view is drawn 92 rows high and shown on 184 lines: every
   other line the copper moves Layer 2's Y scroll (NextReg 0x17) back a row,
   so each row shows twice; lines 184-191 show rows 92-99 (the panel's
   colour, under the HUD) */
static void copper_doubling(void)
{
    uint16_t l;
    copper_begin();
    copper_move(0x17, 0);
    for (l = 2; l < 184; l += 2) {
        copper_wait(l - 1, 50);
        copper_move(0x17, (uint8_t)(192 - l / 2));
    }
    copper_wait(183, 50);
    copper_move(0x17, 192 - 92);
    copper_wait(191, 50);
    copper_move(0x17, 0);
    copper_end();
}

static void palette(uint8_t w)
{
    uint8_t k;
    for (k = 0; k < 32; k++) pal_set(1, k, rgb9(ld_palettes[w][k]));
}

static void flight(uint8_t kind, uint8_t w, uint8_t lv)
{
    int32_t steps;
    uint32_t h;
    uint16_t f0 = ticks;
    LOGIC();
    ld_new_level(&level, w, lv);
    h = flight_hash_kind(&level, kind, FLIGHT_STEPS, &steps);
    ter_done();
    zx_log("ld: flight %u %u %u %ld %d %ld %08lX frames=%u\n", kind, w, lv, steps, level.outcome, level.result.score, h,
           (uint16_t)(ticks - f0));
}

int main(void)
{
    uint16_t loops = 0, frames = 0;
    int32_t t = 0;
    uint8_t acc = 0;
    next_init();
    l2_init();
    ZXN_WRITE_REG(0x15, 0x2B);
    zx_border(INK_BLACK);
    palette(0);
    copper_doubling();
    frame_init();
    frame_hook = tick_isr;
    zx_log("ld: ready\n");
    if (key_down(IN_KEY_SCANCODE_t)) { flight(1, 1, 5); flight(0, 0, 1); }
    VIEW();
    view_init();
    vf_bank0 = 22;
    LOGIC();
    ld_new_level(&level, 0, 1);
    view_reset();
    for (;;) {
        uint8_t f = frame_wait(), steps;
        l2_flip();
        LOGIC();
        acc += f;
        if (acc > 8) acc = 8;
        steps = acc >> 1;
        acc &= 1;
        while (steps--) {
            uint8_t k;
            view_tick(&level);
            for (k = 0; k < 4; k++) {
                LdInput in;
                in.throttle = FX(0.37); in.pitch = (t / 300) % 2 ? FONE : 0; in.roll = 0;
                ld_step(&level, &in);
                t++;
            }
            if (level.outcome) { ld_new_level(&level, (uint8_t)(t % 3), 1); view_reset(); }
        }
        view_camera(&level);
        VIEW();
        view_draw(&level, (uint8_t)t);
        loops++;
        frames += f;
        if (frames >= 100) {
            zx_log("ld: perf fps10=%u polys=%u t=%ld\n", (uint16_t)(loops * 500u / frames), view_polys, t);
            loops = frames = 0;
        }
    }
    return 0;
}
