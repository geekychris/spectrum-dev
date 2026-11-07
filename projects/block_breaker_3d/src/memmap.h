/*
 * memmap.h - where Block Breaker 3D's parts live (see main.c).
 *
 * port: zsdcc's code for the game is about 45K, more than the 28K below the
 * IM 2 table, so it is in parts that the main loop maps in turn (as Boing
 * Pinball and Spectral Keep do): the rules or the renderer at 0x0000, the
 * HUD or the self-test at 0x6000. Code in a bank calls only main memory and
 * code that is mapped whenever it runs (the self-test, in page 85, calls the
 * rules in bank 40).
 */
#ifndef MEMMAP_H
#define MEMMAP_H

#include <arch/zxn.h>

#define LOGIC_PAGE 80                   /* bank 40: bb_core.c, at 0x0000 while the logic runs */
#define DRAW_PAGE  82                   /* bank 41: render.c, gfx_data.c, at 0x0000 while drawing */
#define HUD_PAGE   84                   /* hud.c at 0x6000 */
#define TEST_PAGE  85                   /* test_cases.c at 0x6000 during the self-test */
#define TILE_PAGE  10                   /* bank 5's first half (the tilemap) at 0x4000 */

#define MAP_LOGIC() do { ZXN_WRITE_MMU0(LOGIC_PAGE); ZXN_WRITE_MMU1(LOGIC_PAGE + 1); } while (0)
#define MAP_DRAW()  do { ZXN_WRITE_MMU0(DRAW_PAGE); ZXN_WRITE_MMU1(DRAW_PAGE + 1); } while (0)

/* render.c's windows: Layer 2 (and sprite patterns being read) through slot
   2, the depth plane and the clean tunnel through slot 3 */
#define WIN_T 2
#define WIN_Z 3

#endif
