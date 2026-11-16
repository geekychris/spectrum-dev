/*
 * Drone Courier for the Spectrum Next - port of amiga_mcp/examples/
 * drone_courier, an Amiga game on the shared amiga3d engine: fly a parcel
 * drone over a city of towers, collecting parcels at the depot and landing
 * them on customers' roofs before the clock runs out.
 *
 * Memory map (everything is in the .nex; this only maps it):
 *   0x0000-0x3FFF  bank 20: the original's rules (dc_core.c), the sound, over the ROM
 *                  (the tiles are made from the ROM font first); bank 21 instead while
 *                  the 3D view is drawn (view.c, its polygon pool)
 *   0x4000-0x5FFF  bank 5's first page: the tilemap (nexttile.h)
 *   0x6000-0x7FFF  page 44: the game shell and the text (game.c, hud.c);
 *                  Layer 2's pages are mapped here while drawing (l2poly,
 *                  which puts it back); page 45 instead while the
 *                  original's tests run (test_cases.c)
 *   0x8000-0xEFFF  the 3D view, the sound, the SDK, the variables;
 *                  0xF000- the IM 2 table, the stack
 *   banks 9-14     Layer 2 (two 256 x 192 screens); bank 23 the sprite
 *                  patterns (tools/gen_gfx.py)
 *
 * Hold T while it starts: the original's tests (tests/test_cases.c) run on
 * the Next, the long autopilot game against tests/vectors.h's hash, and
 * the result is logged ("dc: selftest").
 */
#include <string.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "dc.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "sound.h"

static uint16_t test_pass, test_fail;

static void check(int cond, const char *name)
{
    if (cond) test_pass++;
    else {
        test_fail++;
        zx_log("dc: FAIL %s\n", name);
    }
}

static void selftest(void)
{
    test_pass = test_fail = 0;
    ZXN_WRITE_MMU3(45);                      /* page 45: the tests (and their DCGame) */
    run_tests(check);
    ZXN_WRITE_MMU3(44);
    zx_log("dc: selftest %u passed, %u failed\n", test_pass, test_fail);
}

/* profiling: raster lines (~1,800 T-states each) since a mark, across frames */
static uint16_t raster(void) { return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F); }

void clk_mark(clk_t *c)
{
    do {
        c->f = frame_ticks;
        c->r = raster();
    } while (c->f != frame_ticks);
}

uint16_t clk_lines(const clk_t *c)
{
    clk_t n;
    clk_mark(&n);
    return (uint16_t)(uint8_t)(n.f - c->f) * 312 + n.r - c->r;
}

/* the view's bank 21 at 0x0000 while drawing, then the rules' bank 20 */
void view_draw(const DCGame *g, uint16_t cam_yaw)
{
    ZXN_WRITE_MMU0(42);
    ZXN_WRITE_MMU1(43);
    view_scene(g, cam_yaw);
    ZXN_WRITE_MMU0(40);
    ZXN_WRITE_MMU1(41);
}

void view_clear(void)
{
    ZXN_WRITE_MMU0(42);
    ZXN_WRITE_MMU1(43);
    view_blank();
    ZXN_WRITE_MMU0(40);
    ZXN_WRITE_MMU1(41);
}

int main(void)
{
    static const char *const names[HS_ENTRIES] = {"DRN", "PKG", "FLY", "BOX", "ZIP"};
    uint8_t i;
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x2B);               /* sprites on, over the border, clipped; order S U L */
    hud_init();                              /* the ROM's font: before anything is mapped over it */
    zx_border(INK_BLACK);
    pal_set8(0, 16, L2_TRANSPARENT);         /* the border's ULA colour transparent: the fallback shows */
    ZXN_WRITE_REG(0x4A, 0x05);               /* the fallback colour: the panel's 0x123 */
    l2_init();                               /* clears through slots 0-1 and puts the ROM back */
    ZXN_WRITE_MMU0(42);                      /* bank 21: the view */
    ZXN_WRITE_MMU1(43);
    view_init();
    ZXN_WRITE_MMU0(40);                      /* bank 20: the rules */
    ZXN_WRITE_MMU1(41);
    ZXN_WRITE_MMU3(44);                      /* page 44: the shell */
    ay_init();
    snd_init();
    input_init();
    input_fast();
    if (key_down(IN_KEY_SCANCODE_t)) selftest();
    hs_init(200);                            /* port: a table (the original keeps a best score) */
    for (i = 0; i < HS_ENTRIES; i++) memcpy(hs_table[i].name, names[i], 4);
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    game_main();
    return 0;
}
