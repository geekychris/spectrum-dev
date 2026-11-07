/*
 * nextruntest: the SDK's l2wrun (column-run images on Layer 2's 320 x 256,
 * mirrored too, across 8K pages), dma_copy_pages and sprtext (big text from
 * sprites). test.py checks Layer 2, the copied pages and the sprites.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxlog.h"
#include "zxgame.h"
#include "nextgfx.h"
#include "l2wrun.h"
#include "sprtext.h"

extern const uint8_t test_img[];

#define L2_BANK  9
#define COPY_PAGE 70                    /* dma_copy_pages: Layer 2's pages 1-2 to 70-71 */

int main(void)
{
    uint8_t next;
    next_init();
    dma_fill_banks(L2_BANK, 0, 5);
    l2w_init(L2_BANK);
    l2wr_blit(test_img, 10, 0, 100);    /* columns 10.. (across 32), pens 100..102 */
    l2wr_blit(test_img, 150, 1, 200);   /* mirrored: columns 150 down (across 128) */
    zx_log("run: blitted\n");
    dma_copy_pages(COPY_PAGE, L2_BANK * 2, 2);
    zx_log("run: copied\n");
    spr_init();
    sprtext_init(0, "AB!");
    sprtext_colour(1, 0x1C0);
    next = sprtext_show(5, 100, 50, "A B!", 1, 1);
    zx_log("run: done next=%u width=%u\n", next, sprtext_width("A B!", 1));
    for (;;) ;
}
