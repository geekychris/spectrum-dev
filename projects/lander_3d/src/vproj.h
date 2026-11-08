/* vproj.asm: the ground's grid points onto the screen and its cells' pens
   (vfill.h vf_v, vf_col), set up by view.c ground(); vproj_model.c is its
   C model for the host */
#ifndef VPROJ_H
#define VPROJ_H
#include <stdint.h>
extern int16_t vp_dx0, vp_dz0, vp_cell, vp_camy, vp_cp, vp_sp, vp_i0, vp_j0;
extern uint8_t vp_n, vp_page, vp_rpp, vp_pen0;
extern uint16_t vp_rowb, vp_hbf, vp_cdiv;
extern int16_t vp_hb12;
void vp_project(void);
#endif
