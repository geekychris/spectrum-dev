/* spr_pattern_dma (nextgfx.h): a sprite pattern by DMA, in a file of its
   own so games that don't call it don't link the DMA code */
#include <stdint.h>
#include <z80.h>
#include "nextgfx.h"

void spr_pattern_dma(uint8_t slot, const uint8_t *pixels256)
{
    z80_outp(0x303B, slot & 63);
    dma_to_port(0x5B, pixels256, 256);
}
