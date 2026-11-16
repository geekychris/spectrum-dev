/* Latched presses for input2 (see input2.h): a key or Kempston direction
   that went down between two scans (a tap shorter than a slow game frame)
   still shows in the next scan. In a file of its own: games that don't
   latch don't link it. */
#include <stdint.h>
#include <z80.h>
#include <intrinsic.h>
#include "input2.h"

static uint8_t seen[8], seen_kemp[2];

/* from frame_hook (the interrupt, every frame): what is down now */
void in2_sample(void)
{
    uint8_t i, v;
    uint16_t port = 0xFEFE;
    for (i = 0; i < 8; i++, port = (port << 1) | 0x01FE) seen[i] |= ~z80_inp(port) & 0x1F;
    for (i = 0; i < 2; i++) {
        if (!in2_has_kemp[i]) continue;
        v = z80_inp(i ? 0x0037 : 0x001F);
        if (!(v & 0xE0)) seen_kemp[i] |= v & 0x1F;
    }
}

void in2_scan_latched(void)
{
    uint8_t i;
    in2_scan();
    intrinsic_di();
    for (i = 0; i < 8; i++) {
        in2_rows[i] &= ~seen[i];
        seen[i] = 0;
    }
    for (i = 0; i < 2; i++) {
        in2_kemp[i] |= seen_kemp[i];
        seen_kemp[i] = 0;
    }
    intrinsic_ei();
}
