/* Orb Hunter's memory banks (16K) on the Next: what the .nex loads and
   what the game uses (src/gfx_data.asm puts the data there) */
#ifndef BANKS_H
#define BANKS_H
#include <stdint.h>
#define L2_BANK     16      /* Layer 2, 320 x 256: banks 16-20 */
#define WORLD_BANK  21      /* the world as levels_init() makes it (gfx/world.bin) */
#define WORK_BANK   22      /* the game's copy */
#define GFX_BANK    23      /* tiles and the title (tools/gen_gfx.py) */
#define MUSIC_BANK  24      /* the song's rows (tools/make_music.py) */
#define TITLE_BANK  25      /* the title picture: banks 25-29 (tools/gen_gfx.py) */
#define CODE_BANK   30      /* the rules' code (game.c), at 0x0000-0x3FFF */

/* data is mapped through MMU slot 2 (0x4000, normally page 10) and slot 3
   (0x6000, page 11): the ULA screen's bank, unused (Layer 2 covers it) */
#define WIN2 ((uint8_t *)0x4000)
#define WIN3 ((uint8_t *)0x6000)
#endif
