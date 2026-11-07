/*
 * Ace Pilot for the Spectrum Next - port of amiga_mcp/examples/ace_pilot.
 *
 * game.c is the original's rules (50 ticks a second); render.c draws the
 * view on Layer 2. This file is the Next's frame: input, the fixed-timestep
 * loop, the double buffer, title and game over.
 *
 * Memory:
 *   0x0000-0x3FFF  bank 16 (pages 32, 33): render.c, mapped while drawing
 *                  (the ROM otherwise)
 *   0x6000-0x7FFF  Layer 2's pages while drawing (l2line, l2poly)
 *   0x8000-0xEFFF  this, the rules, the SDK, the shared tables
 *   0xF000-        the IM 2 table, the stack
 *   banks 9-14     Layer 2 (two 256 x 192 buffers)
 * Code in a bank only calls main memory.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "nexttile.h"
#include "input2.h"
#include "game.h"
#include "ace_input.h"
#include "render.h"

void sfx_gunfire(void) {}
void sfx_explosion(void) {}
void sfx_hit(void) {}
void sfx_die(void) {}

#define DRAW() do { ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33); } while (0)
#define ROM()  do { ZXN_WRITE_MMU0(0xFF); ZXN_WRITE_MMU1(0xFF); } while (0)

static GameWorld world;
static uint8_t game_state = STATE_TITLE;
static in2_pad pad1;

/* the original's keys and stick -> its input flags */
static UWORD read_input(void)
{
    UWORD f = 0;
    in2_update(&pad1);
    if (pad1.held & IN_LEFT) f |= INPUT_LEFT;
    if (pad1.held & IN_RIGHT) f |= INPUT_RIGHT;
    if (pad1.held & IN_UP) f |= INPUT_UP;
    if (pad1.held & IN_DOWN) f |= INPUT_DOWN;
    if (pad1.held & IN_FIRE) f |= INPUT_FIRE;
    return f;
}

static void draw_title(void)
{
    l2_clear(0);
    l2_text5_center(128, 40, "ACE", 0xE0, 6);
    l2_text5_center(128, 90, "PILOT", 0xFF, 6);
    l2_text5_center(128, 160, "PRESS FIRE", 0xFC, 2);
    l2_flip();
}

int main(void)
{
    next_init();
    l2_init();
    in2_init();
    pad1.src = IN2_QAOP | IN2_CURSOR | IN2_SINCLAIR2 | IN2_KEMPSTON1 | IN2_SPACE;
    frame_init();
    render_view(0, 0, 255, 191);
    draw_title();
    zx_log("ace: ready\n");

    for (;;) {
        uint8_t ticks = frame_wait(), t;
        UWORD in;
        if (ticks > 6) ticks = 6;
        l2_flip();
        in2_scan();
        in = read_input();
        if (game_state == STATE_TITLE) {
            if (pad1.pressed & IN_FIRE) {
                game_init(&world);
                game_state = STATE_PLAYING;
                zx_log("ace: start\n");
            }
            continue;
        }
        for (t = 0; t < ticks; t++) game_update(&world, in, 0);
        l2_clear(0);                     /* maps slots 0-1 itself: before DRAW() */
        DRAW();
        render_scene(&world, 0);
        ROM();
    }
}
