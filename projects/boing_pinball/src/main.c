/*
 * Boing Pinball for the Spectrum Next: memory map and start-up.
 *
 *   0x0000-0x3FFF  bank 20: the rules (pb_rules.c), over the ROM
 *   0x4000-0x5FFF  page 44: the table's data (table_0.c, sin_tab.c), over the ULA
 *                  screen (the ULA is off); Layer 2 pages are mapped here
 *                  briefly to draw
 *   0x6000-0x7FFF  page 46: the front end (front.c)
 *   0x8000-0xEFFF  the physics, the SDK, the C library
 *   0xF000-        the IM 2 table, the stack
 *   banks 30-35    the pictures (data.asm)
 * Everything is in the .nex; this only maps it.
 */
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"

extern void front_main(void);

int main(void)
{
    ZXN_WRITE_MMU0(40);
    ZXN_WRITE_MMU1(41);
    ZXN_WRITE_MMU2(44);
    ZXN_WRITE_MMU3(46);
    next_init();
    frame_init();
    front_main();
    return 0;
}
