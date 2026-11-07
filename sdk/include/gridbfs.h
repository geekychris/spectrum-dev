/*
 * gridbfs.h - grid pathfinding for chasers (Lode Runner-style guards): the
 * first step of a shortest path from any cell to one target cell, through a
 * breadth-first distance field from the target that is kept while neither
 * the target nor the grid changes, and grown only as far as a query needs.
 *
 * Each cell has flags: which ways it can be left (GB_L/R/U/D) and whether it
 * can be entered (GB_IN). A step from p to its neighbour m is allowed when p
 * has the leaving bit and m has GB_IN. Moves off the grid never happen.
 *
 *   gb_init(cells, queue, w, h)  cells[GB_CELLS(w, h)], queue[GB_QUEUE] at a
 *                                256-byte boundary (a ring of 128 cells); all
 *                                cells start with no flags
 *   gb_set(x, y, flags)          a cell's flags (the field is redone if they change)
 *   gb_target(x, y)              where the paths go (the field is redone if it moves)
 *   gb_step(x, y)                the first step from (x, y): GB_L, GB_R, GB_U or
 *                                GB_D; 0 at the target; GB_NONE without a path
 *   gb_dist(x, y)                its distance to the target (255: none known)
 *
 * The step is the one an ordinary breadth-first search from (x, y) would take
 * when it tries the neighbours in the order L, R, U, D (Bullion Dash's
 * guards): BFS finds the shortest path that comes first in that order, so the
 * first step is the first direction whose neighbour is one step closer. The
 * start cell needs no GB_IN. Paths longer than 254 steps count as none.
 *
 * The queue holds the search's frontier (cells at the current distance and
 * the next): Bullion Dash's 150 levels need 32 at most; past 128 gb_overflow
 * is set and queries answer GB_NONE.
 *
 * The search loop is in asm (gridbfs_run.asm, ~300 T-states a cell); a
 * C version builds on the host.
 */
#ifndef ZX_GRIDBFS_H
#define ZX_GRIDBFS_H

#include <stdint.h>

#define GB_L    0x01
#define GB_R    0x02
#define GB_U    0x04
#define GB_D    0x08
#define GB_IN   0x10
#define GB_NONE 0xFF

/* cells: two bytes (flags, distance) per cell, one padding column, a padding
   row above and below */
#define GB_CELLS(w, h) (2 * ((w) + 1) * ((h) + 2))
#define GB_QUEUE 256             /* bytes, at a 256-byte boundary */

extern void gb_init(uint8_t *cells, uint16_t *queue, uint8_t w, uint8_t h);
extern void gb_set(uint8_t x, uint8_t y, uint8_t flags);
extern uint8_t gb_get(uint8_t x, uint8_t y);
extern void gb_target(uint8_t x, uint8_t y);
extern uint8_t gb_step(uint8_t x, uint8_t y);
extern uint8_t gb_dist(uint8_t x, uint8_t y);
extern uint16_t gb_searched;    /* cells taken from the queue since gb_init (profiling) */
extern uint8_t gb_overflow;     /* the frontier outgrew the queue (see above) */

#endif
