/* made by tools/gen_gfx.py - do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>
#define ROOM_BANK 30      /* the room: 320 x 256 Layer 2, 5 banks */
#define SPR_BANK  35      /* sprite patterns */
#define SPR_PATTERNS 29
#define PAT_BARREL 0   /* 2 x 2 */
#define PAT_PLAYER0 4   /* 2 x 3 */
#define PAT_PLAYER1 10   /* 2 x 3 */
#define PAT_PLAYER2 16   /* 2 x 3 */
#define PAT_PLAYER3 22   /* 2 x 3 */
#define PAT_JEWEL 28   /* 1 x 1 */
#define ROOM_COLOURS 38
#define SPR_COLOURS 41
extern const uint16_t room_pal[ROOM_COLOURS];
extern const uint16_t spr_pal[SPR_COLOURS];
#endif
