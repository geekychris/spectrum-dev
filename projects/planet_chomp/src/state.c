/* the game state the logic, the view and the HUD share: in main memory, so
   every part of the program sees it whatever is mapped (see main.c) */
#include "pc.h"
#include "game.h"
#include "render.h"

Game g;
PcView pv;
uint8_t pc_bot;                  /* test: the bot steers (game.c bot_update) */
uint16_t bot_key;
uint8_t crumb[CELLS], key[CELLS];
int16_t nest_dist[CELLS];
uint8_t mz_open[CELLS];
int16_t rd_wall[MAX_WALLS];
int16_t rd_nwalls;
uint8_t rd_dbg;
int16_t rd_cull = 2500;          /* port: Q14 dot added to the horizon tests: only cells within ~70 deg of the camera */
uint8_t rd_thin = 1;             /* port: walls this thin on screen are one quad (draw.asm) */
#ifndef __SDCC
PcWork pc_work;                  /* on the Next: bank 17 (mem.asm) */
#endif
