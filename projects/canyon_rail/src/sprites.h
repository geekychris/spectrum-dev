/* sprites.h - Canyon Rail's hardware sprites (sprites.c) */
#ifndef SPRITES_H
#define SPRITES_H
#include <stdint.h>
#include "spr_views.h"
#include "cr.h"

void spr_setup(void);                   /* the sprite palette, transparency (once) */
void spr_begin(void);                   /* a frame's list (after render_scene: its camera) */
/* an object at world (x, y, z): view, frame; rmul: its size in units for the
   bursts (drawn at 1 unit), 1 otherwise */
void spr_world(uint8_t view, uint8_t frame, int32_t x, int32_t y, int32_t z, uint8_t rmul);
void spr_screen(uint8_t view, int16_t x, int16_t y);   /* at a picture position, over the rest */
void spr_end(void);                     /* order, patterns into the free half, attributes ready */
void spr_present(void);                    /* after the flip: the attributes */
void spr_hide_all(void);
void spr_objects(const CRGame *g);      /* the frame's targets, bolts, bursts, guardian, sight */
#endif
