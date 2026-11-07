/* spl_zmask (nextsprl.h): a 4-bit pattern cut where l2wpoly's depth plane
   is nearer. In a file of its own: it pulls in l2wpoly. */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextsprl.h"
#include "l2wpoly.h"

uint16_t spl_zmask(uint8_t *dst, const uint8_t *src, int16_t x, int16_t y, uint8_t key, uint8_t zpage)
{
    uint8_t c, r, page = 0xFF, p, mask;
    uint16_t left = 0;
    int16_t sx;
    uint8_t r0 = 0, r1 = 16;
    memcpy(dst, src, 128);
    if (y < 0) r0 = (uint8_t)(-y);
    if (y > 240) r1 = (uint8_t)(256 - y);
    for (c = 0, sx = x; c < 16; c++, sx++) {
        uint8_t *d = dst + (c >> 1), *z;
        mask = (c & 1) ? 0x0F : 0xF0;
        if (sx < 0 || sx >= 320) {
            for (r = 0; r < 16; r++, d += 8) if (*d & mask) left++;
            continue;
        }
        p = (uint8_t)(sx >> 5);
        if (p != page) { page = p; ZXN_WRITE_REG(0x50 + l2wp_zslot, zpage + p); }
        z = (uint8_t *)(((uint16_t)l2wp_zslot << 13) + ((uint16_t)(sx & 31) << 8) + (uint8_t)(y + r0));
        d += r0 * 8;
        for (r = r0; r < r1; r++, d += 8, z++) {
            if (!(*d & mask)) continue;
            if (*z < key) *d &= ~mask;
            else left++;
        }
    }
    ZXN_WRITE_REG(0x50 + l2wp_zslot, l2wp_zrestore);
    return left;
}
