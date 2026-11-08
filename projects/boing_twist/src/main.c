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

uint8_t rom_font[768];                   /* copied from the ROM while slot 0 has it */

int main(void)
{
    memcpy(rom_font, (const void *)0x3D00, sizeof(rom_font));
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x09);               /* sprites, tilemap over Layer 2 (S U L) */
    l2_init();
    logic_map();                             /* the text, the tables and the rules from here on */
    ZXN_WRITE_REG(0x4A, 0);                  /* the border: black */
    pal_set8(0, 16, 0xE3);                   /* (the ULA's border colour shows the fallback) */
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
