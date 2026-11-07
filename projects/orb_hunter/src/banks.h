/* Orb Hunter's memory banks (16K) on the Next: what the .nex loads and
   what the game uses (src/gfx_data.asm puts the data there) */
#ifndef BANKS_H
#define BANKS_H
#define L2_BANK     16      /* Layer 2, 320 x 256: banks 16-20 */
#define WORLD_BANK  21      /* the world as levels_init() makes it (gfx/world.bin) */
#define WORK_BANK   22      /* the game's copy */
#define GFX_BANK    23      /* tiles and the title (tools/gen_gfx.py) */
#define MUSIC_BANK  24      /* the song's rows (tools/make_music.py) */
#endif
