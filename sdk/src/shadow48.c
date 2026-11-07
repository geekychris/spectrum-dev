/* shadow48.c - shadow -> screen rectangles (the rest is shadow48.asm) */
#include <string.h>
#include "zxgame.h"
#include "shadow48.h"

void shadow_show_rect(uint8_t c0, uint8_t y, uint8_t w, uint8_t h)
{
    while (h--) {
        uint8_t *s = zx_row[y++] + c0;
        memcpy(s, s + 0x2000, w);
    }
}
