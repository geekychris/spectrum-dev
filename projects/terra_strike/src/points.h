/* Terra Strike - shots, shells and particles as hardware sprites (points.c) */
#ifndef POINTS_H
#define POINTS_H

#include <stdint.h>

void points_init(void);                 /* the dot patterns */
void points_show(void);                 /* render_points -> sprites (at the frame's top) */
uint8_t points_pen_of_offset(uint8_t k);
uint8_t points_offset_of_pen(uint8_t pen);/* the sprite palette offset showing a pen */

#define POINT_SPRITES 80                /* sprites 0..79 (port: the rest are the radar's) */
#define RADAR_SPRITE  80                /* 80..127 */
#define PAT_DOT       32                /* 32..36: 1..5 pixel squares, 37: 1 x 4 *//* the pen sprite palette offset k shows */

#endif
