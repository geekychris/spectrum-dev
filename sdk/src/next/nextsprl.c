/* A frame's 4-bit sprites as one list (see nextsprl.h); the depth masking
   is in nextsprl_z.c, so a game without it doesn't link l2wpoly. */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextsprl.h"

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
