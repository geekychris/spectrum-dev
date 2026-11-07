/*
 * tmap.h - one tile map (a room, a level): w x h cells of one byte each
 * (tile ids), row-major, with a row table so a lookup is no multiply.
 * Levels are made on the host by sdk/tools/levelpack.py (ASCII rooms ->
 * zxrle streams) and unpacked here.
 *
 *   tmap_init(cells, w, h)     use cells[w * h] as the map (h <= TMAP_MAX_H)
 *   tmap_load(packed)          unpack a levelpack/zxrle stream into it
 *   TMAP(c, r)                 the cell (no bounds check), an lvalue
 *   tmap_at(c, r, outside)     bounds-checked read; `outside` off the map
 *   tmap_find(id, &c, &r)      first cell with id (row-major); 0 if none
 *   tmap_count(id)             cells with id
 *   tmap_replace(from, to)     every `from` becomes `to`; returns how many
 *
 * Builds on the host too (game logic tests, level solvers).
 */
#ifndef ZX_TMAP_H
#define ZX_TMAP_H

#include <stdint.h>

#define TMAP_MAX_H 32

extern uint8_t *tmap_cells;
extern uint8_t tmap_w, tmap_h;
extern uint8_t *tmap_row[TMAP_MAX_H];

#define TMAP(c, r) (tmap_row[(r)][(c)])

extern void tmap_init(uint8_t *cells, uint8_t w, uint8_t h);
extern uint16_t tmap_load(const uint8_t *packed);
extern uint8_t tmap_at(int16_t c, int16_t r, uint8_t outside);
extern uint8_t tmap_find(uint8_t id, uint8_t *col, uint8_t *row);
extern uint16_t tmap_count(uint8_t id);
extern uint16_t tmap_replace(uint8_t from, uint8_t to);

#endif
