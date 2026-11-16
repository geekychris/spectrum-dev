/* port: what dc_core.c takes from the amiga3d engine's a3d.h, from the SDK's
   bit-exact fix16.h (fmul/fdiv in Z80N asm, fhypot, a3_sin/a3_cos, a3_atan2) */
#ifndef DC_A3D_COMPAT_H
#define DC_A3D_COMPAT_H
#include "fix16.h"
#define FMUL(a, b) fmul((a), (b))
#define FDIV(a, b) fdiv((a), (b))
#endif
