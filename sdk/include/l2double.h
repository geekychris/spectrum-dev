/*
 * l2double.h - show a 256 x 192 Layer 2 drawn only 96 rows high at full
 * height: the copper moves Layer 2's Y scroll (NextReg 0x17) back a row
 * every other line, so display line l shows row l / 2 and every row shows
 * twice. A 3D view then costs half the rows to fill (Planet Chomp, ScAIry
 * Fun and Drone Courier draw this way); sprites and the tilemap stay full
 * height.
 *
 *   l2dbl_copper()   the moves (97 waits and moves, ~195 instructions) into
 *                    the copper list being built: call between copper_begin()
 *                    and copper_end() (nextcopper.h), before any waits for
 *                    lines below 0; the list's other waits must fit in
 *                    line order around them
 *   L2DBL_ROWS       96: draw rows 0..95 (l2p_bot = L2DBL_ROWS for l2poly)
 *
 * Each move waits for the right border of the line before (h = 50), so it
 * shows from the next line on the Next; ZEsarUX shows a move on the line it
 * is made, one line early (see CLAUDE.md), so there the rows pair up one
 * line later. Test: projects/drone_courier (its screenshots' row pairs).
 */
#ifndef L2DOUBLE_H
#define L2DOUBLE_H
#define L2DBL_ROWS 96
extern void l2dbl_copper(void);
#endif
