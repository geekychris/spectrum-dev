/*
 * Harbour Crane for the Spectrum Next: memory map, start-up, the main loop.
 *
 *   0x0000-0x3FFF  bank 20: the rules (hc_core.c, the original's), over
 *                  the ROM (the ROM's font is copied first); the SDK's
 *                  whole-bank DMA copies put the ROM back, mem.h's RULES()
 *                  maps the rules in again
 *   0x4000-0x5FFF  bank 5's first page: the tilemap (map and tiles)
 *   0x6000-0x7FFF  page 44: view.c's tables at 0x6000-0x6A6F, then the game
 *                  shell and the text (game.c, hud.c); l2fill maps Layer 2's
 *                  pages here while it fills and puts page 44 back
 *   0x8000-0xEFFF  the picture (view.c, cv.asm), the sound, the SDK, the
 *                  variables; 0xF000- the IM 2 table, the stack
 *   banks 9-14     Layer 2's two screens (256 x 192, double buffered)
 *   banks 16-18    the backdrop (tools/gen_gfx.py), 27-29 the level backdrop
 *                  (view.c), 25 the crane's sprites, 26 the sound
 * Everything is in the .nex; this only maps it.
 *
 * Each pass: wait for the frame, show the picture drawn last pass (flip),
 * then game_frame(): input, the rules' ticks for the frames that went by,
 * the text, the next picture.
 */
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "game.h"
#include "view.h"
#include "hud.h"
#include "sound.h"
#include "mem.h"
#include "gfx_data.h"

Crane cr;                                    /* in main memory: the rules, the picture and the shell all use them */
HCGame game;

int main(void)
{
    memcpy(rom_font, (const void *)0x3D00, sizeof(rom_font));
    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x33);               /* sprites on, over the border, clipped; order U S L (text on top) */
    ZXN_WRITE_REG(0x1C, 0x02);               /* the sprites' clip: the whole display (the gauges are in the border) */
    ZXN_WRITE_REG(0x19, 0);
    ZXN_WRITE_REG(0x19, 159);
    ZXN_WRITE_REG(0x19, 0);
    ZXN_WRITE_REG(0x19, 255);
    zx_border(INK_BLACK);
    pal_set8(0, 16, L2_TRANSPARENT);         /* the border's ULA colour see-through: the fallback shows */
    ZXN_WRITE_REG(0x4A, PANEL_RGB8);         /* the border: the original's panel colour (pen 2) */
    l2_init();
    SHELL();
    VIEW();
    view_init();
    RULES();
    hud_init();
    snd_init();
    frame_init();
    zx_log("hc: ready\n");
    game_init();
    for (;;) {
        uint8_t frames = frame_wait();
        l2_flip();
        game_frame(frames);
    }
    return 0;
}
