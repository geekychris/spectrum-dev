/* the game state the logic, the view and the HUD share: in main memory, so
   every part of the program sees it whatever is mapped (see main.c) */
#include "pc.h"
#include "game.h"
#include "render.h"

Game g;
PcView pv;
uint8_t crumb[CELLS], key[CELLS];
int16_t nest_dist[CELLS];
uint8_t mz_open[CELLS];
int16_t rd_wall[MAX_WALLS];
int16_t rd_nwalls;
uint8_t rd_dbg;                  /* test: 1 no disc, 2 no walls, 4 no crumbs drawn */
#ifndef __SDCC
PcWork pc_work;                  /* on the Next: bank 17 (mem.asm) */
#endif
