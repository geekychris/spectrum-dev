/*
 * plat.h - platformer movement against a tile map (tmap.h): the classic
 * "move X, then Y, test the box's corners, snap to the tile edge" of
 * single-screen platformers, as in Miner Meteor (amiga_mcp/examples),
 * kept exactly so a port plays the same:
 *
 *   plat_move_x   x += vx; blocked when the leading edge (lines y+1 and
 *                 y+bh-1) is in a solid tile: then x snaps to the edge of
 *                 the tile the body was in (a side hit can move it up to a
 *                 tile in one step - the original's behaviour)
 *   plat_move_y   vy += gravity (clamped to max_fall); y += vy; falling onto
 *                 a solid tile (columns x+1 and x+bw-2) snaps the feet to
 *                 the tile top, vy = 0, on_ground = 1; hitting a ceiling
 *                 snaps y to its tile and sets vy = 1
 *
 * Positions are pixels (int16), tiles are 1 << plat_shift pixels, the map
 * is tmap's, `plat_solid[id]` says which tile ids block, and everything off
 * the map is tile `plat_outside`. Coordinates are expected >= 0 (the snap
 * uses >>, the original divides). Builds on the host too.
 */
#ifndef ZX_PLAT_H
#define ZX_PLAT_H

#include <stdint.h>

typedef struct {
    int16_t x, y;           /* top-left, pixels */
    int16_t vx, vy;         /* pixels per step */
    uint8_t on_ground;
} plat_body;

extern uint8_t plat_shift;          /* tile size = 1 << plat_shift (default 4: 16 px) */
extern uint8_t plat_bw, plat_bh;    /* body size (default 16 x 16) */
extern const uint8_t *plat_solid;   /* per tile id: nonzero = solid */
extern uint8_t plat_outside;        /* tile id returned off the map */

extern uint8_t plat_kind_at(int16_t px, int16_t py);
extern uint8_t plat_solid_at(int16_t px, int16_t py);
extern void plat_move_x(plat_body *b);
extern void plat_move_y(plat_body *b, int8_t gravity, int8_t max_fall);

#endif
