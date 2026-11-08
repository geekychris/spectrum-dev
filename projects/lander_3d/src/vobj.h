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
void vo_model(const int8_t *v, uint8_t n, const int16_t *k, const V3s *c, V3s *out);
#endif
