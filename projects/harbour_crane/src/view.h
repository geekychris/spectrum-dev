/* The picture (view.c): the level backdrop, this frame's polygons, the sea. */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "hc.h"
void view_init(void);
void view_rebuild(const HCGame *g);       /* the level backdrop (also done by view_draw when the queue changes) */
void view_draw(const HCGame *g, uint8_t splash_timer, int16_t splash_x);   /* into the back buffer */
extern uint8_t view_polys, view_dropped;
#endif
