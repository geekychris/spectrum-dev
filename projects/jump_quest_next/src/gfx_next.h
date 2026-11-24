/* made by tools/gen_gfx_next.py: do not edit */
#ifndef GFX_NEXT_H
#define GFX_NEXT_H
#include <stdint.h>

extern const uint16_t gfx_pal[16];
#define GFX_T_HEART      64     /* tiles after the level's 64: heart full, empty, black */
#define GFX_T_HEART_EMPTY 65
#define GFX_T_BLACK      66
#define GFX_TILES        67
extern const uint8_t gfx_tiles[GFX_TILES * 32];

/* sprite patterns */
#define GFX_S_PLAYER(ch, left, leg, half) ((((ch) * 2 + (left)) * 3 + (leg)) * 2 + (half))
#define GFX_S_ENEMY(type, frame) (24 + ((type) - 2) * 2 + (frame))   /* ENT_BEETLE.. */
#define GFX_S_ITEM(k)    (32 + (k))                                  /* items_draw's order */
#define GFX_S_DOT        42
#define GFX_SPRITES      43
extern const uint8_t gfx_sprites[GFX_SPRITES * 128];
#endif
