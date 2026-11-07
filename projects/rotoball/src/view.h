/* The two views: Layer 2 floor by palette and copper, sprites (view.c). */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
void view_init(void);       /* palettes, post patterns; after next_init, l2w_init */
void view_prepare(void);    /* work out the next picture from G */
void view_commit(void);     /* show it: right after the frame interrupt */
extern uint16_t view_copper_n;
#endif
