#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "rs.h"
extern int16_t view_x, view_y;
extern uint16_t view_pal[64];
extern uint8_t view_npal, view_palgen;
void view_init(void);
void view_course(uint8_t level);
void view_palette(void);
void view_goto(int16_t cx, int16_t cy);
void view_project(const V3 *p, int16_t *x, int16_t *y);
void view_frame(void);
void view_sprites(void);
void view_sprite_init(void);
#endif
