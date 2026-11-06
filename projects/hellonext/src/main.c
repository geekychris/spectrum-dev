/*
 * hellonext - Spectrum Next hello for the spectrum_dev harness.
 *
 * Reads Next registers (machine id, core version), switches to 28 MHz, draws
 * a 256x192 Layer 2 (8 bpp) picture through an MMU slot, keeps the ULA text
 * visible through Layer 2's transparent colour, and logs input.
 */
#include <stdio.h>
#include <input.h>
#include <intrinsic.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxlog.h"
#include "zxtext.h"

#define L2_TRANSPARENT 0xE3          /* default global transparency colour */

static unsigned int frames;
static unsigned char keys;

/* Layer 2 256x192 lives in three 16K banks starting at NextReg 0x12 (9 after
   NextZXOS boots), i.e. six 8K pages from 2*bank. Map each page at 0x6000
   (MMU slot 3, NextReg 0x53) and fill it. */
static void draw_layer2(void)
{
    unsigned char page, y, *row;
    unsigned int x;
    unsigned char old = ZXN_READ_REG(0x53);
    unsigned char first = ZXN_READ_REG(0x12) * 2;
    for (page = 0; page < 6; page++) {
        ZXN_WRITE_REG(0x53, first + page);
        for (y = 0; y < 32; y++) {                      /* 32 lines of 256 per 8K page */
            unsigned char line = page * 32 + y;
            row = (unsigned char *)(0x6000 + (unsigned int)y * 256);
            for (x = 0; x < 256; x++) {
                if (line < 48)
                    row[x] = L2_TRANSPARENT;            /* ULA text shows through */
                else {
                    unsigned char c = (unsigned char)(((line - 48) / 18) << 5   /* RRR: down */
                                                    | (x >> 5) << 2                /* GGG: across */
                                                    | ((x + line) >> 6 & 3));      /* BB */
                    row[x] = c == L2_TRANSPARENT ? c - 1 : c;
                }
            }
        }
    }
    ZXN_WRITE_REG(0x53, old);
    z80_outp(0x123B, 0x02);                             /* Layer 2 visible */
}

int main(void)
{
    unsigned char id = ZXN_READ_REG(0x00), core = ZXN_READ_REG(0x01);

    ZXN_WRITE_REG(0x07, 0x03);                          /* CPU 28 MHz */
    zx_border(INK_BLACK);
    zx_cls(PAPER_BLACK | INK_WHITE | BRIGHT);
    {
        char msg[32];
        zx_print_at(1, 1, "Hello from the Spectrum Next!");
        sprintf(msg, "machine %u core %u.%u", id, core >> 4, core & 15);
        zx_print_at(1, 3, msg);
    }

    draw_layer2();
    zx_log("hellonext: machine=%u core=%u.%u speed=%u\n", id, core >> 4, core & 15,
           ZXN_READ_REG(0x07) & 3);
    zx_log("hellonext: ready\n");
    intrinsic_ei();

    for (;;) {
        unsigned char k;
        intrinsic_halt();
        frames++;
        k = in_inkey();
        if (k && k != keys) {
            zx_log("hellonext: key %c frame=%u\n", k, frames);
            char msg[20];
            sprintf(msg, "You pressed: %c  ", k);
            zx_print_at(1, 4, msg);
        }
        keys = k;
        if (frames % 250 == 0) zx_log("hellonext: frame %u\n", frames);
    }
}
