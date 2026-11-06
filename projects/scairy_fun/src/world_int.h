/*
 * world_int.h - what world.c, world_traffic.c and world_ai.c share (port:
 * the original world.c was one file, where these were static).
 */
#ifndef WORLD_INT_H
#define WORLD_INT_H

#include "sc.h"

#define SEGF       ((int32_t)SEG_LEN * (1 << FBITS))     /* fine units per segment */
#define CAR_LEN    36
#define CAR_WID    15

static int32_t iabs(int32_t a) { return a < 0 ? -a : a; }
static int16_t iabs16(int16_t a) { return a < 0 ? -a : a; }     /* port: for the 16-bit values */

extern const char *const hz_names[HZ_COUNT];

/* port: a - b out of line, where zsdcc miscompiled the subtraction inline
   (it dropped byte 2 of an int32_t loaded through a pointer) */
int32_t z_minus(int32_t a, int32_t b);

/* world.c */
void push(World *w, int type, int32_t a, int32_t b);
void hurt(World *w, int n, int ev);
void place_car(World *w);
/* world_traffic.c */
int lane_clear(const World *w, int lane, int32_t z, int dz0, int dz1, int skip);
int16_t swerve_to(const World *w, const Obj *o);
void hazards_update(World *w);
void traffic_spawn(World *w);
void objects_update(World *w);
void collide(World *w);
/* world_ai.c */
void car_update(World *w, unsigned in);

#endif
