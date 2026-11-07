/* A frame's 4-bit sprites as one list, and depth masking (see nextsprl.h). */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextsprl.h"
#include "l2wpoly.h"

uint8_t spl_count, spl_first;
static uint8_t shown;                   /* sprites the last list showed */
static uint8_t attr[SPL_MAX * 5];
static uint8_t zero[64];

static void hide(uint8_t first, uint8_t n)
{
    z80_outp(0x303B, first);
    for (; n > 16; n -= 16) dma_to_port(0x57, zero, 64);
    if (n) dma_to_port(0x57, zero, (uint16_t)n << 2);
}

void spl_init(void)
{
    ZXN_WRITE_REG(0x4B, 0x00);
    hide(0, 128);
    shown = 0;
    spl_count = 0;
}

void spl_begin(void) { spl_count = 0; }

uint8_t spl_add(int16_t x, int16_t y, uint8_t pat, uint8_t pal, uint8_t flags)
{
    uint8_t *a;
    if (spl_first + spl_count >= SPL_MAX) return 0;
    if (x <= -16 || x >= 320 || y <= -16 || y >= 256) return 1;
    a = attr + spl_count * 5;
    a[0] = (uint8_t)x;
    a[1] = (uint8_t)y;
    a[2] = (uint8_t)(pal << 4) | (flags & 0x0E) | ((x >> 8) & 1);
    a[3] = 0xC0 | ((pat >> 1) & 63);                  /* visible, 5th byte, N5..N0 */
    a[4] = 0x80 | ((pat & 1) << 6) | ((y >> 8) & 1);  /* 4-bit, N6, Y8 */
    spl_count++;
    return 1;
}

void spl_end(void)
{
    if (spl_count) {
        z80_outp(0x303B, spl_first);
        dma_to_port(0x57, attr, (uint16_t)spl_count * 5);
    }
    if (shown > spl_count) hide(spl_first + spl_count, shown - spl_count);
    shown = spl_count;
}

void spl_patterns(uint8_t pat, const uint8_t *src, uint8_t n)
{
    z80_outp(0x303B, ((pat >> 1) & 63) | ((pat & 1) << 7));
    dma_to_port(0x5B, src, (uint16_t)n << 7);
}

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
