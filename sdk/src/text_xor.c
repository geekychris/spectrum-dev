/* text_xor (zxgame.h): ROM font XORed onto the 48K screen */
#include <stdint.h>
#include "zxgame.h"

void text_xor(uint8_t col, uint8_t y, const char *s, uint8_t tall)
{
    for (; *s && col < 32; s++, col++) {
        const uint8_t *g = (const uint8_t *)0x3C00 + ((uint16_t)(uint8_t)*s << 3);
        uint8_t yy = y, i;
        for (i = 0; i < 8; i++, g++) {
            if (yy < 192) zx_row[yy][col] ^= *g;
            yy++;
            if (tall) {
                if (yy < 192) zx_row[yy][col] ^= *g;
                yy++;
            }
        }
    }
}
