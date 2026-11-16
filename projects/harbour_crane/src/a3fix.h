/*
 * a3fix.h - the amiga3d engine's a3d_fix.h names on the SDK's fix16.h, so
 * the original's rules (hc_core.c) compile unchanged and give its results
 * bit for bit (FMUL and FDIV are 64-bit on the Amiga; fix16 does the same
 * arithmetic with 16 x 16 multiplies).
 */
#ifndef A3FIX_H
#define A3FIX_H
#include "fix16.h"
#define FMUL(a, b)  fmul((a), (b))
#define FDIV(a, b)  fdiv((a), (b))
#endif
