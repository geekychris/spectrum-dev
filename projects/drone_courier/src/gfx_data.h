/* made by tools/gen_gfx.py (see there) */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>
#define SPR_BANK   23           /* sprite patterns, loaded by the .nex */
#define RECIP_PAGE 48           /* FOV * 4096 / z, z < 4096 (uint16 at 0x6000 when mapped there) */
#define SPR_NPAT   55
#define P_ARROW    0            /* 16 headings, 0 = up, clockwise */
#define P_WIND     16
#define P_GLYPH    17
#define SPR_GLYPHS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'!"
#define SPR_INK    1            /* a pattern's ink pixel (+16 x the palette offset) */
#define PAL_SKY    32           /* Layer 2: the sky's 16 steps */
extern const uint16_t gfx_palette[48];
#endif
