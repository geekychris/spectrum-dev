/* made by tools/gen_gfx.py */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>
extern const uint16_t gfx_pal[256];
extern const uint8_t gfx_tiles[10][256];
#define SPR_PAGE 40         /* the sprite patterns' first 8K page */
#define TILE_FLOOR 0
#define TILE_WALL_A 1
#define TILE_WALL_B 2
#define TILE_RUNE 3
#define TILE_OIL 4
#define TILE_HAZARD 5
#define TILE_TORCH 6
#define TILE_CHEST 7
#define TILE_PORTAL_LOCKED 8
#define TILE_PORTAL_OPEN 9
#define SPR_POSE_UP 0
#define SPR_POSE_RIGHT 1
#define SPR_POSE_DOWN 2
#define SPR_POSE_LEFT 3
#define SPR_ENEMY_A 4
#define SPR_ENEMY_B 12
#define SPR_ENEMY_C 20
#define NUM_SPRITE_PATTERNS 28
#endif
