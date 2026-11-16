/* made by tools/gen_gfx.py - do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>

#define WORLD_BANK 16     /* the house's columns, column RLE: banks WORLD_BANK.. */
#define INDEX_PAGE 32  /* the column index (see gen_gfx.py) */
#define WORLD_COLS 1920
#define SPR_BANK 23
#define SPR_PATTERNS 38
#define L2_COLOURS 129
#define PUFF_INDEX 208
#define SP_RJ0 0
#define SP_RJ1 1
#define SP_GUEST0 2
#define SP_GUEST1 3
#define SP_GUEST2 4
#define SP_WANT 5
#define SP_PATIENCE0 6
#define SP_PATIENCE1 7
#define SP_PATIENCE2 8
#define SP_PATIENCE3 9
#define SP_PATIENCE4 10
#define SP_PATIENCE5 11
#define SP_COP00 12
#define SP_COP10 13
#define SP_COP01 14
#define SP_COP11 15
#define SP_ITEM1 16
#define SP_ITEM2 17
#define SP_ITEM3 18
#define SP_ITEM4 19
#define SP_ITEM5 20
#define SP_ITEM6 21
#define SP_ITEM7 22
#define SP_ITEM8 23
#define SP_ITEM9 24
#define SP_ITEM10 25
#define SP_BOING0 26
#define SP_BOING1 27
#define SP_BOING2 28
#define SP_BOING3 29
#define SP_BOING4 30
#define SP_BOING5 31
#define SP_BSHADOW 32
#define SP_PUFF2 33
#define SP_PUFF3 34
#define SP_PUFF4 35
#define SP_PUFF5 36
#define SP_SPARK 37
#define SPR_IMAGES 38
extern const uint16_t l2_pal[L2_COLOURS];
extern const uint16_t spr_pal[256];
extern const char *const room_names[6];
extern const uint8_t sprite_anchor[SPR_IMAGES][2];   /* where (sx, sy) is in each image */
#endif

/* pattern numbers */
#define PAT_RJ0 0   /* 1 x 2 */
#define PAT_RJ1 2   /* 1 x 2 */
#define PAT_GUEST0 4   /* 1 x 2 */
#define PAT_GUEST1 6   /* 1 x 2 */
#define PAT_GUEST2 8   /* 1 x 2 */
#define PAT_WANT 10   /* 1 x 1 */
#define PAT_PATIENCE0 11   /* 1 x 1 */
#define PAT_PATIENCE1 12   /* 1 x 1 */
#define PAT_PATIENCE2 13   /* 1 x 1 */
#define PAT_PATIENCE3 14   /* 1 x 1 */
#define PAT_PATIENCE4 15   /* 1 x 1 */
#define PAT_PATIENCE5 16   /* 1 x 1 */
#define PAT_COP00 17   /* 1 x 2 */
#define PAT_COP10 19   /* 1 x 2 */
#define PAT_COP01 21   /* 1 x 2 */
#define PAT_COP11 23   /* 1 x 2 */
#define PAT_ITEM1 25   /* 1 x 1 */
#define PAT_ITEM2 26   /* 1 x 1 */
#define PAT_ITEM3 27   /* 1 x 1 */
#define PAT_ITEM4 28   /* 1 x 1 */
#define PAT_ITEM5 29   /* 1 x 1 */
#define PAT_ITEM6 30   /* 1 x 1 */
#define PAT_ITEM7 31   /* 1 x 1 */
#define PAT_ITEM8 32   /* 1 x 1 */
#define PAT_ITEM9 33   /* 1 x 1 */
#define PAT_ITEM10 34   /* 1 x 2 */
#define PAT_BOING0 36   /* 1 x 1 */
#define PAT_BOING1 37   /* 1 x 1 */
#define PAT_BOING2 38   /* 1 x 1 */
#define PAT_BOING3 39   /* 1 x 1 */
#define PAT_BOING4 40   /* 1 x 1 */
#define PAT_BOING5 41   /* 1 x 1 */
#define PAT_BSHADOW 42   /* 1 x 1 */
#define PAT_PUFF2 43   /* 1 x 1 */
#define PAT_PUFF3 44   /* 1 x 1 */
#define PAT_PUFF4 45   /* 1 x 1 */
#define PAT_PUFF5 46   /* 1 x 1 */
#define PAT_SPARK 47   /* 1 x 1 */
#define TITLE_COL0 1920   /* the title's columns follow the house's in the index */
#define HUD_PAGE 33   /* the HUD bar: 320 columns of 16 rows */
