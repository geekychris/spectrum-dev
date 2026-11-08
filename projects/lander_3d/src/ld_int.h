/*
 * ld_int.h - what ld_core.c and ld_land.c share (the rules' insides).
 */
#ifndef LD_INT_H
#define LD_INT_H
#include "ld.h"

extern fix r_cr, r_sr, r_cp, r_sp;      /* the rotation's (ld_rot_set) */
void ld_rot_set(fix pitch, fix roll);
void ld_cell_of(const Terrain *T, fix x, fix z, uint8_t *i, uint8_t *j);
void ld_caches_reset(void);             /* a new ground: ld_core.c's kept answers go */
void ld_push_ev(Level *L, int e);
void ld_finish(Level *L, int outcome);
int  ld_contact(Level *L);
void ld_level_load(Level *L, int world, int level);   /* ld_level.c (bank 24) */
#endif
