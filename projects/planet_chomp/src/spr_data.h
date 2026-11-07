/* made by tools/gen_gfx.py: do not edit */
#ifndef SPR_DATA_H
#define SPR_DATA_H
#include <stdint.h>
#define SPR_PAGE 52               /* the sprites' first 8K page */
#define SPR_REC 4864                /* bytes a texture: its sizes */
#define SPR_SIZES 10
#define SPR_SMALL 7                 /* sizes in one pattern */
#define SPR_TEXTURES 32
#define SPR_EXTRA_N 18             /* patterns from the start of page SPR_EXTRA_PAGE: */
#define SPR_EXTRA_PAGE 50         /* popups 200 400 800 1600 (2 each), dots, life */
extern const uint8_t spr_size[SPR_SIZES];      /* pixels across */
extern const uint16_t spr_pal[256];             /* 9-bit */
#endif
