/*
 * Orbit Docking for the Spectrum Next: memory map, start-up, the 50 Hz loop.
 *
 *   0x0000-0x3FFF  bank 20: the rules (od_core.c) and the shell's work,
 *                  bank 21 while the view is drawn (view.c and its tables),
 *                  bank 24 while the text is (hud.c, screens.c), over the ROM
 *   0x4000-0x5FFF  bank 5's first page (the tilemap)
 *   0x6000-0x7FFF  Layer 2's pages, mapped in turn by l2poly
 *   0x8000-0xEFFF  the shell (game.c), the sound, the SDK, the variables; 0xF000- the IM 2 table,
 *                  the stack
 *   banks 9-14     Layer 2 (two screens); 22 the sprite patterns; 23 the sound
 * Everything is in the .nex; this only maps it.
 */
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nextcopper.h"
#include "zxlog.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "sound.h"

const uint16_t view_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0x002, 0xCCD, 0x99A, 0x667, 0xBBC, 0x111, 0x3F5, 0xF33,
    0x889, 0x556, 0xAAB, 0x778, 0x0A8, 0x986, 0x35A, 0x238, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000
};

uint8_t rom_font[768];                   /* (here: copied from the ROM while slot 0 has it) */

static uint16_t rgb9(uint16_t c)
{
    return (uint16_t)(((c >> 9) & 7) << 6 | ((c >> 5) & 7) << 3 | ((c >> 1) & 7));
}

/* port: the view is drawn 96 rows high and shown 192: every other line the
   copper moves Layer 2's Y scroll (NextReg 0x17) back a row, so each row
   shows twice */
static void copper_doubling(void)
{
    uint16_t l;
    copper_begin();
    copper_move(0x17, 0);
    for (l = 2; l < 192; l += 2) {
        copper_wait(l - 1, 50);
        copper_move(0x17, (uint8_t)(192 - l / 2));
    }
    copper_wait(191, 50);
    copper_move(0x17, 0);
    copper_end();
}

int main(void)
{
    uint8_t k;
    memcpy(rom_font, (const void *)0x3D00, sizeof(rom_font));
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x2B);               /* sprites on, over the border, clipped; order S U L */
    l2_init();
    for (k = 0; k < 32; k++) pal_set(1, k, rgb9(view_palette[k]));
    zx_border(INK_BLACK);
    copper_doubling();
    VIEW();
    view_init();
    HUD();
    hud_init();
    snd_init();
    input_init();
    frame_init();
    frame_hook = game_sample;
    input_latch(1);
    zx_log("od: ready kempston=%u\n", input_kempston);
    LOGIC();
    game_init();
    for (;;) {
        uint8_t f = frame_wait();
        l2_flip();
        game_frame(f);
    }
    return 0;
}
