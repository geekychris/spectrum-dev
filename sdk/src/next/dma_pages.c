/*
 * dma_copy_pages (nextgfx.h): 8K pages by zxnDMA, through MMU slots 0 and 1
 * (the ROM comes back), e.g. a band of columns of Layer 2's 320 mode (32
 * columns a page) kept elsewhere in memory.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "nextgfx.h"

void dma_copy_pages(uint8_t dst_page, uint8_t src_page, uint8_t npages)
{
    for (; npages; npages--) {
        ZXN_WRITE_MMU0(src_page++);
        ZXN_WRITE_MMU1(dst_page++);
        dma_copy((void *)0x2000, (const void *)0x0000, 8192);
    }
    ZXN_WRITE_MMU0(0xFF);
    ZXN_WRITE_MMU1(0xFF);
}
