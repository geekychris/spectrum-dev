/* vobj.asm: the view's objects into camera space and onto the screen (with
   vproj.asm's camera, set up by view.c); vproj_model.c has them in C for
   the host */
#ifndef VOBJ_H
#define VOBJ_H
#include <stdint.h>
#include "vfill.h"
typedef struct { int16_t x, y, z; } V3s;
void vo_cam(int16_t dx, int16_t dy, int16_t dz, V3s *out);
void vo_scr(const V3s *p, VfVtx *out);  /* p->z >= 12 */
void vo_scr_n(const V3s *in, VfVtx *out, uint8_t n);
/* a box (lo x y z, hi x y z) turned by k and moved to c: 8 corners (bit 0 x,
   1 y, 2 z at hi) into cam and, unless one is nearer than 12 (returns 1), out */
uint8_t vo_box(const int8_t *lohi, const int16_t *k, const V3s *c, VfVtx *out, V3s *cam);
/* a level octagon round m: off = { a, b, a sin, a cos, b sin, b cos } (view.c
   octagon()); 8 points into cam and, unless one is nearer than 12 (1), out */
uint8_t vo_oct(const V3s *m, const int16_t *off, VfVtx *out, V3s *cam);
void vo_model(const int8_t *v, uint8_t n, const int16_t *k, const V3s *c, V3s *out);
#endif
