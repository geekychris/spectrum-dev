/*
 * Frank the Frog - game constants (ZX Spectrum port of
 * amiga_mcp/examples/frank_the_frog/game.h). Port changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H

#include "amiga_types.h"

/* port: 256x192. 16 columns of 16 px (was 20 x 16 on 320); rows keep the
   original 13, but only the 10 lanes and the home bank are 16 px tall: the
   median and the start strip are 8 px (13 x 16 = 208 lines wouldn't fit). */
#define SCREEN_W 256
#define SCREEN_H 192
#define TILE_W 16
#define TILE_H 16
#define GRID_COLS (SCREEN_W / TILE_W)   /* 16 columns */
#define GRID_ROWS 13

/* Row layout (top to bottom) */
#define ROW_HOME     0    /* Home bank: ponds to hop into */
#define ROW_RIVER_5  1    /* River lanes (top = fast) */
#define ROW_RIVER_4  2
#define ROW_RIVER_3  3
#define ROW_RIVER_2  4
#define ROW_RIVER_1  5
#define ROW_MEDIAN   6    /* Safe zone */
#define ROW_ROAD_5   7    /* Road lanes (top = fast) */
#define ROW_ROAD_4   8
#define ROW_ROAD_3   9
#define ROW_ROAD_2   10
#define ROW_ROAD_1   11
#define ROW_START    12   /* Start area */

/* port: pixel top and height of each row */
extern const UBYTE row_y[GRID_ROWS];
extern const UBYTE row_h[GRID_ROWS];

/* port: positions in 1/16 pixel, so lane speeds can be scaled by 0.8 */
#define X16(px) ((WORD)(px) * 16)

/* Game states */
#define STATE_TITLE    0
#define STATE_PLAYING  1
#define STATE_DYING    2
#define STATE_LEVEL    3
#define STATE_GAMEOVER 4

/* Max objects per lane */
#define MAX_LANE_OBJECTS 6

/* Home ponds on the top bank (irregularly placed) */
#define NUM_HOMES 4

/* Directions */
#define DIR_LEFT  (-1)
#define DIR_RIGHT  1

#endif /* GAME_H */
