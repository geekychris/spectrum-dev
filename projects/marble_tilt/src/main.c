/*
 * Marble Tilt for the Spectrum Next: memory map, start-up, the 50 Hz loop.
 *
 *   0x0000-0x3FFF  bank 20: the rules (mt_core.c), over the ROM
 *   0x4000-0x5FFF  bank 5's first page (the tilemap); the board's tables
 *                  are mapped here while a board is drawn
 *   0x6000-0x7FFF  page 44: the game shell and the text (game.c, hud.c);
 *                  Layer 2's pages are mapped here briefly (view.c, l2poly,
 *                  which put it back)
 *   0x8000-0xEFFF  the drawing (view.c), the sound, the SDK, the variables;
 *                  0xF000- the IM 2 table, the stack
 *   banks 9-14     Layer 2, double buffered; 30-49 the board's tables
 *                  (tools/gen_view.py); 50 sprite patterns; 36.. the sound
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
#include "view_data.h"

/* the border: the original's sky, a band each 16 lines (copper, fallback colour) */
static void sky(void)
{
    uint8_t k;
    zx_border(INK_BLACK);
    pal_set8(0, 16, L2_TRANSPARENT);         /* the border's ULA colour transparent: the fallback shows */
    copper_begin();
    for (k = 0; k < 16; k++) {
        copper_wait(L2Y_TO_LINE(k * 16), 0);
        copper_move(0x4A, view_sky8[k]);
    }
    copper_end();
}

int main(void)
{
    memcpy(rom_font, (const void *)0x3D00, sizeof(rom_font));
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x33);               /* sprites on, over the border; order U S L */
    view_init();                             /* l2_init clears through slots 0-1 and puts the ROM back */
    ZXN_WRITE_MMU0(40);
    ZXN_WRITE_MMU1(41);
    ZXN_WRITE_MMU3(44);
    hud_init();
    sky();
    snd_init();
    input_init();
    frame_init();
    frame_hook = input_sample;
    input_latch(1);
    zx_log("mt: ready kempston=%u\n", input_kempston);
    game_init();
    for (;;) game_frame(frame_wait());
    return 0;
}
