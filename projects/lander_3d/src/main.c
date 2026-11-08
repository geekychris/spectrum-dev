/*
 * Lander 3D for the Spectrum Next: memory map, start-up, the 50 Hz loop.
 *
 *   0x0000-0x3FFF  bank 20: the rules (ld_core.c); bank 22 while the view
 *                  works out the picture (view.c); bank 24 while the text is
 *                  drawn (hud.c, screens.c); Layer 2's back buffer (with
 *                  0x4000-0x5FFF) while vfill draws into it
 *   0x4000-0x5FFF  bank 5's first page (the tilemap); a level's heights
 *                  while they are read (ter.c, vproj.asm)
 *   0x6000-0x7FFF  page 46 (the autopilot) with the rules, page 47 (vfill,
 *                  vproj, vobj) with the view
 *   0x8000-0xEFFF  the shell (game.c), the sound, the SDK, the variables;
 *                  0xF000- the IM 2 table, the stack
 *   banks 9-14     Layer 2 (two screens); 25 the sprite patterns; 26 the
 *                  song's rows; 40-90 the levels (lv_rec.h)
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
#include "vfill.h"
#include "hud.h"
#include "sound.h"


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

int main(void)
{
    uint8_t k;
    memcpy(rom_font, (const void *)0x3D00, 768);   /* (into vf_pv: hud.h) */
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x2B);               /* sprites on, over the border, clipped; order S U L */
    l2_init();
    for (k = 0; k < 2; k++) {                /* rows 92-99 of both screens: the panel's pen */
        l2_fill(0, VROWS, 256, 8, 2);
        l2_flip();
    }
    zx_border(INK_BLACK);
    copper_doubling();
    VIEW();
    view_init();
    vf_bank0 = 22;
    HUD();
    hud_init();
    snd_init();
    frame_init();
    frame_hook = game_sample;
    LOGIC();
    game_init();
    for (;;) {
        uint8_t f = frame_wait();
        l2_flip();
        game_frame(f);
    }
    return 0;
}
