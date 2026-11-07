/* memmap.h - where Pea Shooter Blast's parts live (see main.c) */
#ifndef MEMMAP_H
#define MEMMAP_H
#define RULES_PAGE  80                  /* bank 40: game.c at 0x0000-0x3FFF */
#define LEVELS_PAGE 82                  /* levels.c and level_maps at 0x4000 */
#define UI_PAGE     84                  /* hud.c, font.c, gfx_data.c, probe.c, music.c at 0x6000 */
#define HASH_PAGE   86                  /* state_hash.c at 0x6000 during the scripted run */
#define DRAW2_PAGE  88                  /* title.c, sprites.c at 0x4000 while they run */
#endif
