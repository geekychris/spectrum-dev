/* hit48.h: C entry (the scan is hit48.asm) */
#include <stdint.h>
#include "hit48.h"

extern int16_t hit_x, hit_y;
extern uint8_t hit_r, hit_n;
extern const hit_circle *hit_list;
extern uint8_t hit_scan(void) __z88dk_fastcall;

uint8_t hit_first(int16_t x, int16_t y, uint8_t r, const hit_circle *list, uint8_t n)
{
    hit_x = x;
    hit_y = y;
    hit_r = r;
    hit_list = list;
    hit_n = n;
    return hit_scan();
}
