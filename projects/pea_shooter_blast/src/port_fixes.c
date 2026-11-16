/*
 * port_fixes.c - port: the original can't be finished (ORIGINAL_BUGS.md,
 * tools/reach.c): its jump (g_tune.jump_power 3.50, a 24-pixel rise) clears
 * none of the 2-tile walls levels.c's comments call "jumpable", and level 1's
 * metal block at columns 95-99 closes the ground route from the floor to
 * its platform. The fixes: the jump at 4.50 (through the original's own
 * tunables), and a 2-tile doorway in that block at the floor, as levels.c
 * cuts in its other walls.
 */
#include "port_fixes.h"

void port_fixes(GameState *gs)
{
    WORD x, y;
    g_tune.jump_power = 450;
    for (y = 12; y < 14; y++)
        for (x = 95; x < 100; x++) level_maps[0][y][x] = TILE_EMPTY;
    (void)gs;
}
