/*
 * l2pictest - sdk l2pic on the Next: a 1000-column picture
 * (tools/gen.py) viewed at a list of positions, forwards, backwards and in
 * jumps. After each view it logs the position, the scroll and the columns
 * copied, and waits 10 frames; test.py reads Layer 2 and the scroll
 * registers then and checks them against the picture.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "l2pic.h"

static const int16_t views[] = { 0, 5, 100, 419, 425, 420, 300, 100, 0, 680, 361, 680, -20 };

int main(void)
{
    uint8_t i, k;
    next_init();
    l2w_init(9);
    frame_init();
    l2pic_init(32, 1000, 16);
    for (i = 0; i < sizeof(views) / sizeof(views[0]); i++) {
        uint16_t s;
        l2pic_copied = 0;
        s = l2pic_view(views[i]);
        l2pic_scroll(s);
        zx_log("lp: view %d scroll %u copied %u\n", views[i], s, l2pic_copied);
        for (k = 0; k < 10; k++) frame_wait();
    }
    l2pic_reset();                         /* after a reset the whole view is copied again */
    l2pic_copied = 0;
    l2pic_scroll(l2pic_view(680));
    zx_log("lp: view 680 scroll %u copied %u reset\n", 680 % 320, l2pic_copied);
    for (k = 0; k < 10; k++) frame_wait();
    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
