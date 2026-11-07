/* Back to the ROM's IM 1 after frame_init() (see zxgame.h): for code that
   returns to BASIC. Its own file so games that never return don't link it. */
#include <intrinsic.h>
#include "zxgame.h"

void frame_exit(void)
{
    intrinsic_di();
    __asm
        ld a,0x3F
        ld i,a
        im 1
    __endasm;
    intrinsic_ei();
}
