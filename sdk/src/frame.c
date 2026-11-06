/* Frame timer on the 50 Hz interrupt (ROM IM 1 handler; it advances the
   FRAMES system variable at 23672). See zxgame.h. */
#include <stdint.h>
#include <intrinsic.h>
#include "zxgame.h"

#define FRAMES (*(volatile uint8_t *)23672)

uint16_t frame_count;
uint16_t frame_overruns;
static uint8_t frame_last;

void frame_init(void)
{
    intrinsic_ei();
    frame_count = 0;
    frame_overruns = 0;
    frame_last = FRAMES;
}

void frame_wait(void)
{
    uint8_t d;
    while (FRAMES == frame_last) intrinsic_halt();
    d = FRAMES - frame_last;
    if (d > 1) frame_overruns += d - 1;
    frame_last = FRAMES;
    frame_count++;
}
