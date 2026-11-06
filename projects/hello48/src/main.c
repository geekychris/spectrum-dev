/*
 * hello48 - smallest useful 48K program for the spectrum_dev harness.
 *
 * Text on the 32x24 terminal, a bouncing attribute block, keyboard input,
 * 50 Hz frame pacing with HALT, and zx_log lines a test can wait for.
 */
#include <stdio.h>
#include <input.h>
#include <intrinsic.h>
#include <arch/zx.h>
#include "zxlog.h"

static unsigned int frames;
static unsigned char bx = 3, by = 8;
static signed char dx = 1, dy = 1;
static unsigned char keys;

static void draw_block(unsigned char attr)
{
    unsigned char *a = zx_cxy2aaddr(bx, by);
    a[0] = attr; a[1] = attr; a[32] = attr; a[33] = attr;
}

int main(void)
{
    zx_border(INK_BLUE);
    zx_cls(PAPER_WHITE | INK_BLACK);
    printf("\x16\x02\x02" "Hello from hello48!");
    printf("\x16\x04\x02" "Press any key...");

    /* colour bars along the bottom (attribute memory) */
    {
        unsigned char x;
        unsigned char *a = zx_cxy2aaddr(0, 22);
        for (x = 0; x < 32; x++) a[x] = a[x + 32] = (x / 4) << 3 | BRIGHT;
    }

    zx_log("hello48: ready\n");
    intrinsic_ei();                 /* ROM IM1 interrupt: HALT waits for vsync */

    for (;;) {
        unsigned char k;
        intrinsic_halt();
        frames++;

        draw_block(PAPER_WHITE | INK_BLACK);
        if (bx == 0 || bx == 30) dx = -dx;
        if (by == 6 || by == 20) dy = -dy;
        bx += dx; by += dy;
        draw_block(PAPER_RED | BRIGHT);

        k = in_inkey();
        if (k && k != keys) {
            zx_log("hello48: key %c frame=%u\n", k, frames);
            printf("\x16\x04\x02" "You pressed: %c        ", k);
        }
        keys = k;

        if (frames % 250 == 0) zx_log("hello48: frame %u\n", frames);
    }
}
