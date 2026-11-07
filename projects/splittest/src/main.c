/*
 * splittest - the split-screen and two-player modules: nextsplit (copper
 * panes with their own sprite clip windows, a row-exact palette switch),
 * sprite groups with zoom, palette offset and sprg_hide (nextspr), DMA
 * palette loads (palfx_load_bytes), the copper list uploaded by DMA
 * (nextcopper), and input2. test.py checks the screen and the log.
 *
 * The screen: Layer 2 (320 x 256) all colour 5; its first palette has 5 =
 * blue, the second 5 = green, and the copper shows the second from row 200.
 * Sprite pattern 0 is solid colour 1 (red; with palette offset 1, colour
 * 17 = yellow). Pane A is rows 0..119, pane B rows 121..255:
 *   sprite 0: 16 x 16 at (40, 112): rows 112..119 show, 120..127 are cut
 *             by pane A's clip, rows 121..127 are pane B's (shown)
 *   sprites 1-4: a 2 x 2 group at (100, 40), zoom x2, palette offset 1:
 *             64 x 64 yellow at 100..163, 40..103
 *   sprites 5-6: shown, then hidden by sprg_hide (at 200, 40)
 * Then each frame the two players' input2 bits are logged on a change,
 * until ENTER.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nextspr.h"
#include "nextsplit.h"
#include "palfx.h"
#include "input2.h"
#include "zxlog.h"

static uint8_t pat[4 * 256];
static uint8_t l2bytes[2 * 2];

int main(void)
{
    in2_pad p1 = {IN2_WASD | IN2_SINCLAIR2 | IN2_KEMPSTON1, 0, 0};
    in2_pad p2 = {IN2_IJKL | IN2_SINCLAIR1, 0, 0};
    uint8_t last1 = 0xFF, last2 = 0xFF;
    uint16_t c;

    next_init();
    spr_init();
    ZXN_WRITE_REG(0x15, 0x23);                  /* sprites on, over the border, clipped there; S L U */
    l2w_init(9);
    l2w_fill(0, 0, 320, 256, 5);

    /* Layer 2 colour 5: blue in the first palette, green in the second (by DMA) */
    c = RGB9(0, 0, 7);
    l2bytes[0] = (uint8_t)(c >> 1);
    l2bytes[1] = c & 1;
    palfx_load_bytes(PAL_L2, 5, l2bytes, 2);
    c = RGB9(0, 7, 0);
    l2bytes[0] = (uint8_t)(c >> 1);
    l2bytes[1] = c & 1;
    palfx_load_bytes(PAL_L2 | PAL_SECOND, 5, l2bytes, 2);
    pal_set(2, 1, RGB9(7, 0, 0));
    pal_set(2, 17, RGB9(7, 7, 0));

    memset(pat, 1, sizeof(pat));
    sprg_upload(0, pat, 4);

    split_begin();
    split_pane(0, 119, SPLIT_SPRITES);
    copper_move(0x43, 0x00);                    /* (at the restart) the first palette */
    split_pane(121, 255, SPLIT_SPRITES);
    split_row(200);
    copper_move(0x43, 0x04);                    /* Layer 2: the second palette from row 200 */
    zx_log("lib: copper n=%u\n", split_end());

    sprg_show_ex(0, 40, 112, 1, 1, 0, 0, 0, 0);
    sprg_show_ex(1, 100, 40, 2, 2, 0, 0, 1, 1);
    sprg_show_ex(5, 200, 40, 2, 1, 0, 0, 0, 0);
    sprg_hide(5, 2);

    in2_init();
    frame_init();
    zx_log("lib: ready\n");
    for (;;) {
        frame_wait();
        in2_scan();
        in2_update(&p1);
        in2_update(&p2);
        if (p1.held != last1 || p2.held != last2) {
            zx_log("lib: in2 p1=%u p2=%u\n", p1.held, p2.held);
            last1 = p1.held;
            last2 = p2.held;
        }
        if (in2_bits(IN2_ENTER)) break;
    }
    zx_log("lib: done\n");
    for (;;) frame_wait();
    return 0;
}
