/* Frame timer on the 50 Hz interrupt, IM 2 (see zxgame.h).
 *
 * The ROM's IM 1 handler is not used: it writes system variables through IY,
 * which compiled C code does not preserve (that corrupts memory). Our handler
 * only counts frames and can call frame_hook (e.g. sound_tick) every frame.
 * Memory 0xF000-0xF1F3 holds the vector table and jump: keep programs and
 * data below 0xF000 (project.mk checks the link map).
 */
#include <stdint.h>
#include <string.h>
#include <intrinsic.h>
#include "zxgame.h"

extern volatile uint8_t frame_ticks;
extern void frame_isr(void);

uint16_t frame_count;
uint16_t frame_overruns;
static uint8_t frame_last;

void frame_init(void)
{
    intrinsic_di();
    memset((void *)0xF000, 0xF1, 257);
    *(uint8_t *)0xF1F1 = 0xC3;                          /* JP frame_isr */
    *(uint16_t *)0xF1F2 = (uint16_t)frame_isr;
    __asm
        ld a,0xF0
        ld i,a
        im 2
    __endasm;
    frame_count = 0;
    frame_overruns = 0;
    frame_last = frame_ticks;
    intrinsic_ei();
}

uint8_t frame_wait(void)
{
    uint8_t d;
    while (frame_ticks == frame_last) intrinsic_halt();
    d = frame_ticks - frame_last;
    if (d > 1) frame_overruns += d - 1;
    frame_last = frame_ticks;
    frame_count++;
    return d;
}
