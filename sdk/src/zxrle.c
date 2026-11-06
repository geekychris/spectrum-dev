/* Run-length unpacking (see zxrle.h). Builds on the host too. */
#include <stdint.h>
#include <string.h>
#include "zxrle.h"

uint16_t rle_unpack(uint8_t *dst, const uint8_t *src)
{
    uint8_t *start = dst, c, n;
    while ((c = *src++) != 0x80) {
        if (c < 0x80) {
            n = c + 1;
            memcpy(dst, src, n);
            src += n;
        } else {
            n = c - 0x7E;
            memset(dst, *src++, n);
        }
        dst += n;
    }
    return (uint16_t)(dst - start);
}
