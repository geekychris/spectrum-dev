/* made by tools/gen_sprites.py */
#ifndef SPRITE_DATA_H
#define SPRITE_DATA_H
#include <stdint.h>
#define SPR_NPAT 11
#define SPR_NPAL 4
#define PAT_MARBLE 0
#define PAL_MARBLE 0
#define PAT_STEEL 6
#define PAL_STEEL 1
#define PAT_BLOB 7
#define PAL_BLOB 2
#define PAT_SHADOW 8
#define PAL_SHADOW 3
#define PAT_SPARK 10
#define PAL_SPARK 3
#define MARBLE_R_PX 5.71
extern const uint8_t spr_patterns[];
extern const uint16_t spr_palettes[];
#endif
