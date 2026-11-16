/* l2double.h: Layer 2 rows shown twice by the copper (see the header) */
#include <stdint.h>
#include "nextcopper.h"
#include "l2double.h"

void l2dbl_copper(void)
{
    uint16_t l;
    copper_move(0x17, 0);                            /* (the list starts in the top border) */
    /* line l (odd) and l + 1 show row (l + 1) / 2 - ... : scroll 192 - (l + 1) / 2 */
    for (l = 1; l < 192; l += 2) {
        copper_wait(l - 1, 50);                      /* the right border of line l - 1 */
        copper_move(0x17, (uint8_t)(192 - (l + 1) / 2));
    }
    copper_wait(191, 50);
    copper_move(0x17, 0);
}
