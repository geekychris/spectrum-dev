/*
 * gridmove.h - maze-chase movement on a tile grid (sdk/src/gridmove.c).
 *
 * The rules of the classic maze chase, as Dot Chase has them: actors move in
 * world units of 8 per tile with an 8.8 sub-unit fraction, turn only when
 * centred on a tile, wrap through a side tunnel, and enemies pick, at each
 * tile centre, the open neighbour nearest their target tile without
 * reversing (ties: up, left, down, right). The game draws the world units
 * at any scale (Dot Chase: 6 px a tile on the 48K).
 *
 * The actor's position is a grid_body, which a game struct can start with.
 */
#ifndef GRIDMOVE_H
#define GRIDMOVE_H

#include <stdint.h>

#define GRID_TILE   8                  /* world units per tile */

#define GRID_NONE  (-1)                /* directions */
#define GRID_RIGHT  0
#define GRID_DOWN   1
#define GRID_LEFT   2
#define GRID_UP     3
#define GRID_OPPOSITE(d) (((d) + 2) & 3)

extern const int8_t grid_dx[4];
extern const int8_t grid_dy[4];

typedef struct {
    int16_t tile_x, tile_y;            /* tile under the actor's centre */
    int16_t pixel_x, pixel_y;          /* world position of its top-left (tile * 8 when centred) */
    int16_t sub_x, sub_y;              /* 8.8 fraction, 0..255 */
} grid_body;

/* place on a tile, centred, no fraction */
extern void grid_place(grid_body *b, int16_t tx, int16_t ty);
/* centred on a tile (both world coordinates multiples of 8) */
#define grid_centered(b) ((((b)->pixel_x | (b)->pixel_y) & 7) == 0)
/* move `speed` (8.8 units a call, up to 7.99 units) in dir; a move that
   would pass a tile centre stops on it, so grid_centered() sees every centre
   (Dot Chase's original only tested for exact centres and skipped some);
   recomputes the tile (centre / 8, rounding toward zero like C division);
   past -8 or wrap_w the actor reappears at the other side (wrap_w = grid
   width * 8; 0 = no wrap) */
extern void grid_move(grid_body *b, int16_t dir, int16_t speed, int16_t wrap_w);

/* squared tile distance; |dx|, |dy| <= 127 */
extern uint16_t grid_dist2(int16_t x1, int16_t y1, int16_t x2, int16_t y2);
/* the direction an enemy at tile (tx, ty) heading `dir` takes toward
   (target_x, target_y): the open neighbour (open(x, y) != 0) nearest the
   target, never the reverse, ties to up, left, down, right; `dir` if none */
typedef uint8_t (*grid_open_fn)(int16_t x, int16_t y);
extern int8_t grid_choose(int16_t tx, int16_t ty, int8_t dir,
                          int16_t target_x, int16_t target_y, grid_open_fn open);

#endif
