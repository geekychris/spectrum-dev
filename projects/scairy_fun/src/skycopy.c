/*
 * skycopy.c - the skyline's panorama into the view (render.h). Resident:
 * it maps the panorama's pages over the renderer's bank while it copies.
 */
#include <stdint.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "sc.h"
#include "render.h"

/* the skyline's panorama (render.h) into back buffer rows ytop.., from its
   column p0 (wrapping); the renderer's pages are mapped back afterwards */
void sky_copy(uint16_t p0, int16_t ytop)
{
    const uint8_t *src = 0;             /* (the panorama at 0x0000) */
    uint8_t r, page = 0xFF;
    uint16_t n1 = SKY_W - p0;
    if (n1 > 256) n1 = 256;
    ZXN_WRITE_MMU0(SKY_PAGE); ZXN_WRITE_MMU1(SKY_PAGE + 1);
    for (r = 0; r < SKY_PH; r++, src += SKY_W) {
        int16_t y = ytop + r;
        uint8_t *dst;
        if (y < 0) continue;
        if ((uint8_t)(y >> 5) != page) {
            page = (uint8_t)(y >> 5);
            ZXN_WRITE_REG(0x53, l2_back * 2 + page);
        }
        dst = (uint8_t *)0x6000 + ((y & 31) << 8);
        dma_copy(dst, src + p0, n1);
        if (n1 < 256) dma_copy(dst + n1, src, 256 - n1);
    }
    ZXN_WRITE_MMU0(36); ZXN_WRITE_MMU1(37); ZXN_WRITE_MMU3(11);
}
