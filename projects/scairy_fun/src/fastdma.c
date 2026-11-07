/*
 * fastdma.c - zxnDMA copies with the program sent by one OTIR (resident).
 * The SDK's dma.c sends it with 16 z80_outp calls (~3300 T-states a
 * transfer's set-up; this ~1700). Kept here, not in the SDK: Spectral Keep's
 * test bot takes another path when the SDK's DMA gets faster.
 */
#include <stdint.h>
#include "fastdma.h"

static uint8_t prog[16] = {
    0x83,                                       /* WR6: disable */
    0x7D, 0, 0, 0, 0,                           /* WR0: A -> B, A address + length follow */
    0x54, 0x02,                                 /* WR1: A memory, incrementing; 2-cycle timing */
    0x50, 0x02,                                 /* WR2: B memory incrementing (0x50) or a fixed I/O port (0x68) */
    0xAD, 0, 0,                                 /* WR4: burst, B address follows */
    0x82,                                       /* WR5: stop at the end */
    0xCF,                                       /* WR6: load */
    0x87                                        /* WR6: enable: runs now */
};

static void run(const void *src, uint16_t dst, uint16_t len, uint8_t wr2)
{
    prog[2] = (uint16_t)src & 0xFF;
    prog[3] = (uint16_t)src >> 8;
    prog[4] = len & 0xFF;
    prog[5] = len >> 8;
    prog[8] = wr2;
    prog[11] = dst & 0xFF;
    prog[12] = dst >> 8;
#ifdef __SDCC
    __asm
        ld hl,_prog
        ld bc,0x106B                            ; 16 bytes to port 0x6B
        otir
    __endasm;
#endif
}

void fdma_copy(void *dst, const void *src, uint16_t len)
{
    if (len) run(src, (uint16_t)dst, len, 0x50);
}

void fdma_to_port(uint16_t port, const void *src, uint16_t len)
{
    if (len) run(src, port, len, 0x68);
}
