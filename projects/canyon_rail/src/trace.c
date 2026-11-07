/* trace.c - the trace build (make TRACE=1): tools/trace_main.c on the Z80 */
#ifdef CR_TRACE
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "../tools/trace_main.c"

static volatile uint16_t ints;
static void tick(void) { ints++; }

int main(void)
{
    ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33);     /* the rules' bank (main.c) */
    next_init();
    frame_init();
    frame_hook = tick;
    zx_log("trace: start\n");
    trace_games();
    zx_log("trace: done ints=%u\n", ints);
    for (;;) ;
}
#endif
