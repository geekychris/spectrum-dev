/*
 * The Next's zxnDMA (port 0x6B): memory copy and fill, memory to an I/O
 * port (see nextgfx.h).
 * A transfer runs in burst mode and the CPU waits for it: about 2 T-states
 * a byte, against 21 for LDIR.
 */
#include <stdint.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"

static void dma(const void *src, void *dst, uint16_t len, uint8_t wr1, uint8_t wr2)
{
    z80_outp(0x6B, 0x83);                       /* WR6: disable */
    z80_outp(0x6B, 0x7D);                       /* WR0: A -> B, A address + length follow */
    z80_outp(0x6B, (uint16_t)src & 0xFF);
    z80_outp(0x6B, (uint16_t)src >> 8);
    z80_outp(0x6B, len & 0xFF);
    z80_outp(0x6B, len >> 8);
    z80_outp(0x6B, wr1);                        /* WR1: A memory, fixed (0x64) or incrementing (0x54), timing follows */
    z80_outp(0x6B, 0x02);                       /* 2-cycle timing */
    z80_outp(0x6B, wr2);                        /* WR2: B memory incrementing (0x50) or a fixed I/O port (0x68), timing follows */
    z80_outp(0x6B, 0x02);
    z80_outp(0x6B, 0xAD);                       /* WR4: burst, B address follows */
    z80_outp(0x6B, (uint16_t)dst & 0xFF);
    z80_outp(0x6B, (uint16_t)dst >> 8);
    z80_outp(0x6B, 0x82);                       /* WR5: stop at the end */
    z80_outp(0x6B, 0xCF);                       /* WR6: load */
    z80_outp(0x6B, 0x87);                       /* WR6: enable: runs now */
}

void dma_copy(void *dst, const void *src, uint16_t len)
{
    if (len) dma(src, dst, len, 0x54, 0x50);
}

void dma_fill(void *dst, uint8_t value, uint16_t len)
{
    static uint8_t v;
    v = value;
    if (len) dma(&v, dst, len, 0x64, 0x50);
}

/* memory -> one I/O port, e.g. 0x5B (sprite patterns) after selecting the
   pattern slot on 0x303B, or 0x57 (sprite attributes) */
void dma_to_port(uint16_t port, const void *src, uint16_t len)
{
    if (len) dma(src, (void *)port, len, 0x54, 0x68);
}

/* 16K banks, a page at a time through MMU slots 0 and 1 (the ROM comes back) */
void dma_copy_banks(uint8_t dst_bank, uint8_t src_bank, uint8_t nbanks)
{
    uint8_t p;
    for (p = 0; p < nbanks * 2; p++) {
        ZXN_WRITE_MMU0(src_bank * 2 + p);
        ZXN_WRITE_MMU1(dst_bank * 2 + p);
        dma(0x0000, (void *)0x2000, 8192, 0x54, 0x50);
    }
    ZXN_WRITE_MMU0(0xFF);
    ZXN_WRITE_MMU1(0xFF);
}

void dma_fill_banks(uint8_t bank, uint8_t value, uint8_t nbanks)
{
    uint8_t p;
    for (p = 0; p < nbanks * 2; p++) {
        ZXN_WRITE_MMU1(bank * 2 + p);
        dma_fill((void *)0x2000, value, 8192);
    }
    ZXN_WRITE_MMU1(0xFF);
}
