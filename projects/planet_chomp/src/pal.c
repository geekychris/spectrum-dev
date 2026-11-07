/* Layer 2 palette (render.h's indices): the original's colours (render_cel.c)
   in 9 bits */
#include <stdint.h>
#include "render.h"

#define C9(r, g, b) ((uint16_t)(((((r) * 7 + 127) / 255) << 6) | ((((g) * 7 + 127) / 255) << 3) | (((b) * 7 + 127) / 255)))
#define SIDE(l) C9(158 * (120 + 150 * (2 * (l) + 1) / 16) / 256, 92 * (120 + 150 * (2 * (l) + 1) / 16) / 256, \
                   41 * (120 + 150 * (2 * (l) + 1) / 16) / 256)

const uint16_t rd_pal[C_COUNT] = {
    C9(4, 6, 16),                                  /* space */
    C9(128, 128, 148), C9(170, 170, 190), C9(212, 212, 232), C9(255, 255, 255),   /* stars */
    C9(52, 56, 84),                                /* the planet */
    C9(120, 90, 170),                              /* the nest */
    C9(255, 214, 170),                             /* crumbs */
    C9(255, 199, 115),                             /* wall tops */
    SIDE(0), SIDE(1), SIDE(2), SIDE(3), SIDE(4), SIDE(5), SIDE(6), SIDE(7)       /* wall sides, by |side . view| */
};
/* the level-clear flash (the original's RD_WALL_FLASH) */
const uint16_t rd_pal_flash[9] = {
    C9(255, 255, 255), C9(235, 240, 255), C9(235, 240, 255), C9(235, 240, 255), C9(235, 240, 255),
    C9(235, 240, 255), C9(235, 240, 255), C9(235, 240, 255), C9(235, 240, 255)
};
