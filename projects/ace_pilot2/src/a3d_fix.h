/* a3d_fix.h - port: the amiga3d engine's fixed point is the SDK's fix16.h
   (bit for bit); FMUL / FDIV are its fmul / fdiv */
#ifndef A3D_FIX_H
#define A3D_FIX_H
#include "fix16.h"
#define FMUL(a, b)  fmul((a), (b))
#define FDIV(a, b)  fdiv((a), (b))
#endif
