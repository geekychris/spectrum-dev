/* made by tools/gen_gfx.py: do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>
#include <arch/zx.h>

extern const uint8_t gfx_tile_cells[64 * 8];
extern const uint8_t gfx_tile_attr[64];
#define GFX_PLAYER_H 28
extern const uint8_t gfx_player[2 * 2 * 3 * 28 * 2];
extern const uint8_t gfx_enemy[4 * 2 * 16 * 2];
extern const uint8_t gfx_item[10 * 16 * 2];
extern const uint8_t gfx_enemy_ink[4];
extern const uint8_t gfx_item_ink[10];
extern const uint8_t gfx_heart[16];

#endif
