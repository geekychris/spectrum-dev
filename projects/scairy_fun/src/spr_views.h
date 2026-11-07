/* made by tools/gen_sprites.py */
#ifndef SPR_VIEWS_H
#define SPR_VIEWS_H
#include <stdint.h>

enum { SV_CAR, SV_CAR_FRONT, SV_TRUCK, SV_TRUCK_FRONT, SV_TRACTOR, SV_CYCLIST, SV_DEER, SV_DEER_SIDE, SV_CONE, SV_TYRE, SV_ROCK, SV_AMBULANCE, SV_GOAT, SV_GOAT_SIDE, SV_PED, SV_PED_SIDE, SV_PLAYER, SV_PLAYER_L, SV_COUNT };
#define SPR_NSIZES 16
#define SPR_BANK 40
#define SPR_F0_X1000 2400   /* pixels per unit of size 0, x 1000 */
/* shape: cols - 1 (bit 0), rows - 1 (bit 1) of 16 x 16 patterns, scale (bits
   2-3: x1..x8); anchor: the foot's position in the unscaled image; the
   first pattern (128 bytes each) */
typedef struct { uint8_t shape, ax, ay; uint16_t first; } SprSize;
#define SPR_COLS(s) (((s)->shape & 1) + 1)
#define SPR_ROWS(s) ((((s)->shape >> 1) & 1) + 1)
#define SPR_SCALE(s) ((s)->shape >> 2)
extern const SprSize spr_tab[SV_COUNT * SPR_NSIZES];
#endif
