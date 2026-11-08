/*
 * Boing Twist for the Spectrum Next: start-up and the 50 Hz loop. The
 * memory map is in bt.h; everything else is in the .nex.
 */
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "hud.h"
#include "sound.h"
#include "game.h"
#include "bt.h"
#include "nextcopper.h"


/* port: Layer 2 is drawn 96 rows high and shown 192: every other line the
   copper moves its Y scroll (NextReg 0x17) back a row, so each row shows
   twice (Orbit Docking's; one more move at the end so the last line shows
   row 95 too) */
static void copper_doubling(void)
{
    uint16_t l;
    copper_begin();
    copper_move(0x17, 0);
    for (l = 2; l <= 192; l += 2) {
        copper_wait(l - 1, 50);
        copper_move(0x17, (uint8_t)(192 - l / 2));
    }
    copper_end();
}

int main(void)
{
    /* the ROM's font, for the text: into SAVE_PAGE (free until a twist) while the ROM is in */
    ZXN_WRITE_REG(0x53, SAVE_PAGE);
    memcpy((void *)0x6000, (const void *)0x3D00, 768);
    ZXN_WRITE_REG(0x53, 11);
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x09);               /* sprites, tilemap over Layer 2 (S U L) */
    l2_init();
    logic_map();                             /* the text, the tables and the rules from here on */
    zx_border(INK_BLACK);
    copper_doubling();
    hud_init();
    snd_init();
    input_init();
    frame_init();
    frame_hook = game_sample;
    input_latch(1);
    zx_log("bt: ready kempston=%u\n", input_kempston);
    game_init();
    for (;;) {
        uint8_t f = frame_wait();
        game_after_wait();
        game_frame(f);
    }
    return 0;
}
