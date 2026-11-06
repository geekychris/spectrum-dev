/*
 * Spectral Keep for the Spectrum Next: memory map, start-up, input and the
 * 50 Hz loop (the original's main_os4.c).
 *
 *                  while the logic runs     while drawing
 *   0x0000-0x3FFF  bank 39: room.c, game.c   bank 40: scene.c
 *   0x4000-0x5FFF  a window: sprite frames, Layer 2 columns, sound streams,
 *                  the stage, the tilemap (bank 5, page 10 between uses)
 *   0x6000-0x7FFF  page 82: world.c, probe.c the room's blob (palette,
 *                                           pieces, draw list); page 84:
 *                                           hud.c while it draws
 *   0x8000-0xEFFF  this, the SDK, the level data, the variables;
 *                  0xF000- the IM 2 table, the stack
 *   banks 9-13     Layer 2 (320 x 256); 16..33 the pictures, 36..38 the
 *                  sound; page 83 the stage (masked sprites put together)
 *
 * port: zsdcc's code for the original's rules is about 41K, more than the
 * 28K below the IM 2 table, so the program is in three parts that the loop
 * maps in turn. Code in one bank only calls the main memory, never the
 * other bank.
 *
 * Each frame: the frame interrupt, then scene_commit() shows what was worked
 * out last frame (sprites, shadows), the logic runs once per frame that
 * passed (fixed timestep, at most 4), then the HUD and scene_prepare().
 */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <arch/zxn.h>
#include <input.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nextcopper.h"
#include "palfx.h"
#include "zxlog.h"
#include "keep.h"
#include "scene.h"
#include "hud.h"
#include "sound.h"

#define LOGIC() do { ZXN_WRITE_MMU0(78); ZXN_WRITE_MMU1(79); ZXN_WRITE_MMU3(82); } while (0)
#define DRAW()  do { ZXN_WRITE_MMU0(80); ZXN_WRITE_MMU1(81); } while (0)
#define HUD()   ZXN_WRITE_MMU3(84)
#define PANEL_ROW  208                  /* the original's y 200: the status panel */

void keep_log(const char *fmt, ...)
{
    static char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    zx_log_s("keep: ");
    zx_log_s(buf);
}

/* the copper: the status panel's rows show the second palettes (the room
   and the sprites at 3/8: the original's panel shade), the rest the first */
static uint8_t copper_panel = 0xFF;

static void copper_setup(uint8_t panel)
{
    if (panel == copper_panel) return;
    copper_panel = panel;
    copper_begin();
    copper_wait(L2Y_TO_LINE(0), 0);                  /* the top border comes first (line 280) */
    copper_move(0x43, 0x00);
    if (panel) {
        copper_wait(L2Y_TO_LINE(PANEL_ROW), 0);
        copper_move(0x43, 0x0C);                     /* Layer 2 + sprites: second palettes */
    }
    copper_end();
}

/* profiling: raster lines spent in each part of the loop (one line is ~1800
   T-states at 28 MHz), logged every 250 frames */
static uint16_t raster(void) { return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F); }
static uint16_t prof_t, prof[5];
static uint8_t prof_n;
static void lap(uint8_t k)
{
    uint16_t t = raster();
    prof[k] += t >= prof_t ? t - prof_t : t + 312 - prof_t;
    prof_t = t;
}

/* keys and Kempston -> the original's pad bits */
static uint16_t pad_of(uint8_t in)
{
    uint16_t b = 0;
    if (in & IN_UP) b |= B_UP;
    if (in & IN_DOWN) b |= B_DOWN;
    if (in & IN_LEFT) b |= B_LEFT;
    if (in & IN_RIGHT) b |= B_RIGHT;
    if (in & (IN_FIRE | IN_START)) b |= B_A;
    return b;
}

/* port: P is right (QAOP), so pause is H; M is fire, so music is T; Q+E
   (the tour) is V */
static uint16_t extra_keys(void)
{
    uint16_t b = 0;
    if (key_down(IN_KEY_SCANCODE_h)) b |= B_P;
    if (key_down(IN_KEY_SCANCODE_c)) b |= B_C;
    if (key_down(IN_KEY_SCANCODE_t)) b |= B_B;
    if (key_down(IN_KEY_SCANCODE_v)) b |= B_L | B_R;
    return b;
}

int main(void)
{
    uint16_t held, pressed, extra, extra_last = 0;
    uint8_t ticks, flash_shown = 0, i, showing = 0;

    LOGIC();
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x33);                       /* sprites on, over the border, clipped; order U S L */
    l2w_slot = 2;
    l2w_restore = 10;
    l2w_init(9);
    HUD();
    hud_init();
    LOGIC();
    snd_init();
    input_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    zx_log("keep: ready kempston=%u\n", input_kempston);
    game_init();

    for (;;) {
        ticks = frame_wait();
        prof_t = raster();
        DRAW();
        if (showing) scene_commit();
        lap(0);
        {
            uint8_t want = G.clock < G.flash_until ? G.flash_ink : 0;
            if (want != flash_shown) {
                copper_stop();
                scene_flash(want);
                copper_run();
                flash_shown = want;
            }
        }
        if (ticks > 4) ticks = 4;
        LOGIC();
        held = pad_of(input_read());
        pressed = pad_of(input_pressed());
        extra = extra_keys();
        held |= extra;
        pressed |= extra & ~extra_last;
        extra_last = extra;
        for (i = 0; i < ticks; i++) {
            game_step(held, pressed);
            pressed = 0;                             /* an edge counts once */
            snd_tick();
            if (G.room_changed) break;
        }
        probe_update();
        room_grounds();
        lap(1);
        DRAW();
        if (G.room_changed) {
            G.room_changed = 0;
            copper_stop();
            scene_hide();
            scene_room();
            copper_panel = 0xFF;
            HUD();
            hud_reset();
            frame_wait();                            /* the time it took isn't caught up */
            keep_log("room drawn %s overflow=%u\n", R.room->id, scene_overflow);
        }
        lap(2);
        copper_setup(G.state != GS_TITLE || G.tour);
        if (G.state == GS_GAMEOVER || G.state == GS_VICTORY) {
            if (showing) scene_hide();               /* before the HUD blacks the picture */
            showing = 0;
            HUD();
            hud_update();
        } else {
            HUD();
            hud_update();
            lap(3);
            scene_prepare(G.state != GS_DYING);
            showing = 1;
            lap(4);
        }
        if (++prof_n == 100) {
            keep_log("perf lines/100: commit=%u logic=%u room=%u hud=%u prepare=%u overruns=%u\n",
                     prof[0], prof[1], prof[2], prof[3], prof[4], frame_overruns);
            /* the music's chips: mixer and channel A's volume (the test checks they play) */
            keep_log("ay mix0=%u vol0=%u mix1=%u vol1=%u\n", snd_reg(0, 7), snd_reg(0, 8) & 15, snd_reg(1, 7),
                     snd_reg(1, 8) & 15);
            prof[0] = prof[1] = prof[2] = prof[3] = prof[4] = 0;
            prof_n = 0;
        }
    }
    return 0;
}
