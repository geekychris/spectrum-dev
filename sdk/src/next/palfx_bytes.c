/* palfx_load_bytes (see palfx.h): raw palette bytes by DMA, in a file of
   its own so games that don't call it don't link it. */
#include <stdint.h>
#include <z80.h>
#include <arch/zxn.h>
#include "palfx.h"
#include "nextgfx.h"

void palfx_load_bytes(uint8_t sel, uint8_t first, const uint8_t *src, uint16_t nbytes)
{
    ZXN_WRITE_REG(0x43, (ZXN_READ_REG(0x43) & 0x0E) | (sel & 0x70));
    ZXN_WRITE_REG(0x40, first);
    z80_outp(0x243B, 0x44);                 /* NextReg 0x44 (two bytes an entry), then its data by DMA */
    dma_to_port(0x253B, src, nbytes);
}
