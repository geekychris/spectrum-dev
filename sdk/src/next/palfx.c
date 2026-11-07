/* Palettes with effects (see palfx.h). */
#include <stdint.h>
#include <arch/zxn.h>
#include "palfx.h"
#include "nextgfx.h"
#include <z80.h>

uint16_t pal_fx(uint16_t c, uint8_t mul, uint16_t add)
{
    uint8_t r = (uint8_t)(((c >> 6) & 7) * mul >> 3) + ((add >> 6) & 7);
    uint8_t g = (uint8_t)(((c >> 3) & 7) * mul >> 3) + ((add >> 3) & 7);
    uint8_t b = (uint8_t)((c & 7) * mul >> 3) + (add & 7);
    if (r > 7) r = 7;
    if (g > 7) g = 7;
    if (b > 7) b = 7;
    return (uint16_t)r << 6 | (uint16_t)g << 3 | b;
}

static void select(uint8_t sel, uint8_t first)
{
    ZXN_WRITE_REG(0x43, (ZXN_READ_REG(0x43) & 0x0E) | (sel & 0x70));
    ZXN_WRITE_REG(0x40, first);
}

void palfx_load(uint8_t sel, uint8_t first, uint16_t n, const uint16_t *src)
{
    select(sel, first);
    for (; n; n--, src++) {
        ZXN_WRITE_REG(0x44, (uint8_t)(*src >> 1));
        ZXN_WRITE_REG(0x44, *src & 1);
    }
}

void palfx_load_fx(uint8_t sel, uint8_t first, uint16_t n, const uint16_t *src,
                 uint8_t mul, uint16_t add)
{
    select(sel, first);
    for (; n; n--, src++) {
        uint16_t c = pal_fx(*src, mul, add);
        ZXN_WRITE_REG(0x44, (uint8_t)(c >> 1));
        ZXN_WRITE_REG(0x44, c & 1);
    }
}

void palfx_active(uint8_t l2_second, uint8_t spr_second)
{
    ZXN_WRITE_REG(0x43, (ZXN_READ_REG(0x43) & 0x72) | (l2_second ? 0x04 : 0) | (spr_second ? 0x08 : 0));
}

void palfx_load_bytes(uint8_t sel, uint8_t first, const uint8_t *src, uint16_t nbytes)
{
    select(sel, first);
    z80_outp(0x243B, 0x44);                 /* then the data port, by DMA */
    dma_to_port(0x253B, src, nbytes);
}
