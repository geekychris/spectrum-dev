/*
 * world_int.h - what world.c and world_setup.c share (port: the original's
 * world.c is split in two, see world_setup.c).
 */
#ifndef WORLD_INT_H
#define WORLD_INT_H

#include "ts.h"

#define CLEAR        20          /* craft height above ground, world units */
#define CEILING      1900
#define LAND_VY      (-44)       /* fine/frame: slowest safe touchdown */
#define LAND_SPEED   64          /* fine/frame */
#define DIVE_VY      (-40)
#define CLIMB_VY     72
#define SHOT_SPEED   320
#define SHOT_DROP    40
#define SHELL_SPEED  200
#define FUEL_MAX     1000
#define SHIELD_MAX   100

#define F(v)  ((int32_t)(v) << FBITS)
#define FMASK ((WORLD_SIZE << FBITS) - 1)
#define W(v)  ((v) >> FBITS)

int is_target(int type);
void player_to_pad(World *w);

#endif
