/* made by tools/gen_sprites.py */
#ifndef SPR_VIEWS_H
#define SPR_VIEWS_H
#include <stdint.h>

enum { SV_TURRET, SV_DRONE, SV_POD, SV_BOLT, SV_BURST, SV_CORE, SV_NODE, SV_SIGHT, SV_COUNT };
#define SPR_NSIZES 18
#define SPR_BANK 40
#define SPR_F0_X1000 5400      /* pixels per unit of size 0, x 1000 */
#define SPR_BURST_F0_X1000 60000
/* a view: frames, its first size (of the ladder), how many sizes, where in spr_tab;
   a size: cols - 1 (bit 0), rows - 1 (bit 1) of 16 x 16 patterns, scale (bits 2-3:
   x1..x8); the centre in the unscaled image; the first pattern (128 bytes each) */
typedef struct { uint8_t frames, k0, nk; uint16_t at; } SprView;
typedef struct { uint8_t shape, ax, ay; uint16_t first; } SprSize;
#define SPR_COLS(s) (((s)->shape & 1) + 1)
#define SPR_ROWS(s) ((((s)->shape >> 1) & 1) + 1)
#define SPR_SCALE(s) ((s)->shape >> 2)
extern const SprView spr_view[SV_COUNT];
extern const SprSize spr_tab[];
extern const uint16_t spr_pal12[16];
#endif
