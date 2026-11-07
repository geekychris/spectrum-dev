/*
 * Bullion Dash - the original's coordinates (10 x 14 pixel tiles) without
 * dividing: tables of the Spectrum's room pixel for each original pixel,
 * 8/10 across and 8/14 down, which also give the grid cell (pixel >> 3)
 * and whether a position is on a cell boundary (pixel & 7 == 0).
 *
 * Exact for the positions the rules make: x is always even (the player
 * moves 2 pixels, the guards whole tiles); y is even for the player, and a
 * falling guard's odd y is never on a boundary.
 */
#ifndef COORDS_H
#define COORDS_H

#include <stdint.h>
#include "game.h"

#define COORD_X_SIZE (GRID_COLS * TILE_W / 2)           /* x / 2: 0..139 */
#define COORD_Y_SIZE ((GRID_ROWS + 1) * TILE_H)         /* y: 0..237 */

#ifdef __SDCC
#include "memmap.h"
#define coord_x ((uint8_t *)HI_SCALE_X)
#define coord_y ((uint8_t *)HI_SCALE_Y)
#else
extern uint8_t coord_x[COORD_X_SIZE], coord_y[COORD_Y_SIZE];
#endif

void coords_init(void);

#define ROOM_X(px)    (coord_x[(uint16_t)(px) >> 1])    /* room pixel */
#define ROOM_Y(py)    (coord_y[py])
#define CELL_X(px)    (ROOM_X(px) >> 3)                 /* px / TILE_W */
#define CELL_Y(py)    (ROOM_Y(py) >> 3)                 /* py / TILE_H */
#define ALIGNED_X(px) ((ROOM_X(px) & 7) == 0)           /* px % TILE_W == 0 */
#define ALIGNED_Y(py) ((ROOM_Y(py) & 7) == 0 && !((py) & 1))

#endif
