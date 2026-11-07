/*
 * psb.h - the SDK's game layer and the original's game.h together: both
 * define FIX, FIX_INT and FIX_FRAC (8.8 in the SDK, 16.16 in the original);
 * the port uses the original's.
 */
#ifndef PSB_H
#define PSB_H
#include "zxgame.h"
#undef FIX
#undef FIX_INT
#undef FIX_FRAC
#include "game.h"
#endif
