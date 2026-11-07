/*
 * sdktestgrid - gridpack and gridbfs on a 48K (test.py has the references):
 *
 * - gridpack: every grid of src/grids.txt (packed into grids_data.c) is
 *   unpacked and logged row by row
 * - gridbfs: on each grid, from every cell, the first step towards a series
 *   of targets that move one cell at a time (so a field is carried over
 *   when it was complete), then again after one cell's flags change
 */
#include <stdint.h>
#include "zxgame.h"
#include "zxlog.h"
#include "gridpack.h"
#include "gridbfs.h"
#include "grids_data.h"

#define W TG_W
#define H TG_H

/* tile id -> gridbfs flags (test.py has the same table) */
static const uint8_t flags_of[6] = {
    GB_IN | GB_L | GB_R | GB_U | GB_D,      /* . open */
    0,                                      /* # wall */
    GB_IN | GB_R,                           /* > one way right */
    GB_IN | GB_D,                           /* v slide */
    GB_IN | GB_L | GB_R | GB_U | GB_D,      /* H ladder */
    GB_L | GB_R,                            /* - rail: leave sideways, not entered */
};

static uint8_t grid[W * H];
static uint8_t cells[GB_CELLS(W, H)];
static uint16_t *const queue = (uint16_t *)0xF200;     /* a 256-byte page (above the program) */
static const uint8_t targets[4][2] = { {0, 0}, {1, 0}, {1, 1}, {6, 6} };

static void steps(const char *tag, uint8_t tx, uint8_t ty)
{
    static char line[W + 1];
    uint8_t x, y;
    gb_target(tx, ty);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            switch (gb_step(x, y)) {
            case 0:       line[x] = '*'; break;
            case GB_L:    line[x] = 'L'; break;
            case GB_R:    line[x] = 'R'; break;
            case GB_U:    line[x] = 'U'; break;
            case GB_D:    line[x] = 'D'; break;
            default:      line[x] = '-'; break;
            }
        }
        line[W] = 0;
        zx_log("bfs %s %u,%u %u %s\n", tag, tx, ty, y, line);
    }
}

int main(void)
{
    uint8_t g, x, y, t;
    char row[W + 1];

    for (g = 0; g < TG_COUNT; g++) {
        gp_unpack(&tg_set, g, grid);
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) row[x] = '0' + grid[y * W + x];
            row[W] = 0;
            zx_log("grid %u %u %s\n", g, y, row);
        }
        gb_init(cells, queue, W, H);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) gb_set(x, y, flags_of[grid[y * W + x]]);
        for (t = 0; t < 4; t++) steps("a", targets[t][0], targets[t][1]);
        gb_set(4, 0, 0);                    /* a wall appears */
        steps("b", 6, 6);
        zx_log("searched %u %u overflow=%u\n", g, gb_searched, gb_overflow);
    }
    zx_log("ZX:DONE\n");
    for (;;) {}
}
