/* zx_print_at / zx_attr_at (see include/zxtext.h) */
#include <arch/zx.h>
#include "zxtext.h"

#define ROM_FONT ((const unsigned char *)0x3C00)   /* glyph for c at 0x3C00 + 8c */

void zx_print_at(unsigned char col, unsigned char row, const char *s)
{
    while (*s && col < 32) {
        const unsigned char *g = ROM_FONT + (unsigned int)(unsigned char)*s * 8;
        unsigned char *p = zx_cxy2saddr(col, row);
        unsigned char i;
        for (i = 0; i < 8; i++) {
            *p = g[i];
            p += 256;                          /* next pixel line in the cell */
        }
        s++;
        col++;
    }
}

void zx_attr_at(unsigned char col, unsigned char row, unsigned char len, unsigned char attr)
{
    unsigned char *a = zx_cxy2aaddr(col, row);
    while (len-- && col++ < 32) *a++ = attr;
}
