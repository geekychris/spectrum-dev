/* Split screens with the copper (see nextsplit.h). */
#include <stdint.h>
#include "nextcopper.h"
#include "nextsplit.h"

/* a new list; the running one goes on until split_end() replaces it, so a
   list can be rebuilt during the frame and uploaded in the vertical blank */
void split_begin(void) { copper_n = 0; }

/* the horizontal blank at the end of the line above row y: waits after
   pixel 320 of the 448-pixel line (h 0 = the paper's left edge), so the
   next move lands before row y's left border */
void split_row(uint8_t y)
{
    uint16_t line = L2Y_TO_LINE(y);
    copper_wait(line ? line - 1 : 311, 40);
}

static void clip(uint8_t reg, uint8_t y0, uint8_t y1)
{
    copper_move(reg, 0);                /* x 0..159: pixel pairs, the whole 320 */
    copper_move(reg, 159);
    copper_move(reg, y0);
    copper_move(reg, y1);
}

void split_pane(uint8_t y0, uint8_t y1, uint8_t layers)
{
    if (y0) split_row(y0);              /* (at y0 = 0: the restart, before any wait) */
    copper_move(0x1C, layers);          /* reset those layers' clip indices */
    if (layers & SPLIT_L2) clip(0x18, y0, y1);
    if (layers & SPLIT_SPRITES) clip(0x19, y0, y1);
    if (layers & SPLIT_TILES) clip(0x1B, y0, y1);
}
