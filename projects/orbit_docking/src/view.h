/* view.c: the cockpit's view on Layer 2 */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "od.h"

/* Layer 2 pens: the original's (render.c dock_palette) */
enum {
    PEN_BLACK = 0, PEN_WHITE = 1, PEN_PANELBG = 2, PEN_GREY = 3, PEN_SPACE = 4, PEN_STAR = 5, PEN_HUB = 6, PEN_FACE = 8,
    PEN_SLOT = 9, PEN_LIGHT = 10, PEN_RLIGHT = 11, PEN_ARM = 12, PEN_RING = 14, PEN_MARK = 16, PEN_DEBRIS = 17,
    PEN_PANEL = 18, PEN_RED = 26, PEN_YELLOW = 27, PEN_GREEN = 28, PEN_CYAN = 29
};
extern const uint16_t view_palette[32];     /* the original's, 12-bit */

void view_init(void);
void view_draw(const ODGame *g);           /* the scene into the back buffer */
extern uint16_t view_polys;                /* polygons drawn last time */
extern int16_t view_debris_x[OD_MAX_DEBRIS];   /* od_debris_x() >> 16, set before view_draw (the rules are in another bank) */
#endif
