/* The ground for the voxel renderer (ground.c). */
#ifndef GROUND_H
#define GROUND_H
#include <stdint.h>
#include "rr.h"

#define GROUND_SHIFT 4                  /* height cells: 16 units */
#define GROUND_LAT   64                 /* the lattice heights are worked out on: 64 units */

extern int8_t ground_h[128 * 128];      /* heights / 4 */
extern uint8_t ground_pen[32 * 32];     /* a pen per lattice cell, by its slope */
extern int16_t ground_mid[128];         /* the track's middle per height row (cells * 256) */
extern uint8_t ground_stripe[128];

void ground_reset(const RRGame *g, int32_t x, int32_t z);     /* all of it, around (x, z) */
uint8_t ground_follow(const RRGame *g, int32_t x, int32_t z); /* stream in what (x, z) needs; 1 if it did work */
#endif
