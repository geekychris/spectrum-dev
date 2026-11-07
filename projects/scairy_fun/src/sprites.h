/*
 * sprites.h - vehicles, animals, people and cones as hardware sprites
 * (sprites.c): each object is drawn from patterns pre-rendered on the host
 * from the original's models (tools/gen_sprites.py) at the size its depth
 * needs, and hidden where a nearer hill crest covers it.
 */
#ifndef SPRITES_H
#define SPRITES_H

#include <stdint.h>
#include "sc.h"

enum { SP_CAR, SP_CAR_FRONT, SP_TRUCK, SP_TRUCK_FRONT, SP_TRACTOR, SP_CYCLIST, SP_DEER,
       SP_DEER_SIDE, SP_CONE, SP_TYRE, SP_ROCK, SP_AMBULANCE, SP_GOAT, SP_GOAT_SIDE, SP_PED,
       SP_PED_SIDE, SP_PLAYER, SP_COUNT };

void spr_setup(void);                                   /* patterns, palettes */
void spr_frame_begin(const World *w, int s0, int s1);   /* the objects of this frame */
/* one more sprite object at fine (z, d) on the road, lifted by y units */
void spr_add_world(const World *w, uint8_t kind, int32_t z, int16_t d, int16_t y, uint8_t colour);
void spr_segment_done(int seg, int16_t road_top);      /* drawn far to near: what covers what */
void spr_frame_end(const World *w);                     /* the order, the patterns, the list */
void spr_show_frame(void);                              /* after the flip: send it */

#endif
