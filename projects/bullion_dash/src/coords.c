/* Bullion Dash - the coordinate tables (see coords.h) */
#include <stdint.h>
#include "coords.h"

#ifndef __SDCC
uint8_t coord_x[COORD_X_SIZE], coord_y[COORD_Y_SIZE];
#endif

void coords_init(void)
{
    uint16_t v;
    for (v = 0; v < COORD_X_SIZE; v++) coord_x[v] = (uint8_t)(v * 8 / 5);
    for (v = 0; v < COORD_Y_SIZE; v++) coord_y[v] = (uint8_t)(v * 4 / 7);
}
