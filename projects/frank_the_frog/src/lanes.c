/*
 * Frank the Frog - lane objects (traffic + river), from
 * amiga_mcp/examples/frank_the_frog/lanes.c. Port changes are marked "port:";
 * the drawing moved to render.c.
 */
#include <string.h>
#include "game.h"
#include "lanes.h"

Lane lanes[NUM_LANES];

/* Home ponds: centre x of each pond on the top bank (port: x 0.8) */
static const int home_cx[NUM_HOMES] = { 32, 96, 176, 224 };
int home_filled[NUM_HOMES] = {0, 0, 0, 0};

/* port: row geometry (see game.h) */
const UBYTE row_y[GRID_ROWS] = {0, 16, 32, 48, 64, 80, 96, 104, 120, 136, 152, 168, 184};
const UBYTE row_h[GRID_ROWS] = {16, 16, 16, 16, 16, 16, 8, 16, 16, 16, 16, 16, 8};

int lanes_home_cx(int slot)
{
    return home_cx[slot];
}

/* port: speed and spacing scaled by 0.8 (speed kept exactly, in 1/16 px) */
static void setup_lane(int idx, int row, int dir, int speed, int tick_div,
                       UBYTE kind, int is_river, int num, int obj_w, int spacing)
{
    int i;
    Lane *l = &lanes[idx];

    l->row = row;
    l->dir = dir;
    l->speed16 = (speed * 64 + 2) / 5;          /* port: speed * 16 * 0.8 */
    l->tick_div = tick_div;
    l->tick_cnt = 0;
    l->num_objs = num;
    l->kind = kind;
    l->is_river = is_river;

    for (i = 0; i < num && i < MAX_LANE_OBJECTS; i++) {
        l->objs[i].x16 = X16(i * spacing * 4 / 5);   /* port: x 0.8 */
        l->objs[i].width = obj_w;
    }
}

void lanes_init(int level)
{
    int base;

    memset(lanes, 0, sizeof(lanes));

    /* Speed scaling: level 1 feels like classic road-crossing arcade, higher levels get intense */
    base = level;
    if (base > 6) base = 6;

    /* Road lanes (port: object widths for 16-px tiles: bus 36 -> 32) */
    setup_lane(0, ROW_ROAD_1, DIR_RIGHT, 2 + base/2, 1, KIND_CAR_RED,    0, 3, 16, 120);
    setup_lane(1, ROW_ROAD_2, DIR_LEFT,  3 + base/3, 1, KIND_CAR_YELLOW, 0, 3, 16, 110);
    setup_lane(2, ROW_ROAD_3, DIR_RIGHT, 2 + base/2, 1, KIND_CAR_BLUE,   0, 3, 16, 110);
    setup_lane(3, ROW_ROAD_4, DIR_LEFT,  2 + base/3, 1, KIND_BUS,        0, 2, 32, 160);
    setup_lane(4, ROW_ROAD_5, DIR_RIGHT, 3 + base/2, 1, KIND_CAR_RED,    0, 3, 16, 120);

    /* Clamp tick_div minimum to 1 */
    {
        int i;
        for (i = 0; i < NUM_LANES; i++) {
            if (lanes[i].tick_div < 1) lanes[i].tick_div = 1;
        }
    }

    /* River lanes: logs and lily-pad rafts (port: widths x 0.8 to whole bytes) */
    setup_lane(5, ROW_RIVER_1, DIR_RIGHT, 2 + base/3, 1, KIND_LOG,  1, 3, 40, 110);
    setup_lane(6, ROW_RIVER_2, DIR_LEFT,  1 + base/3, 1, KIND_PADS, 1, 3, 32, 100);
    setup_lane(7, ROW_RIVER_3, DIR_RIGHT, 2 + base/2, 1, KIND_LOG,  1, 2, 48, 170);
    setup_lane(8, ROW_RIVER_4, DIR_LEFT,  2 + base/3, 1, KIND_PADS, 1, 3, 32, 110);
    setup_lane(9, ROW_RIVER_5, DIR_RIGHT, 2 + base/2, 1, KIND_LOG,  1, 2, 40, 160);
}

/* port: lanes_tick() is in lanes_tick.asm (same arithmetic: zsdcc's code for
   this loop took half a frame). The C it implements: */
#if 0
/* port (speed, same results): pointer walks instead of indexing, and the
   direction as add/subtract (z88dk's zsdcc turns both into multiply calls) */
void lanes_tick_c(void)
{
    int i, j;
    Lane *l = lanes;

    for (i = 0; i < NUM_LANES; i++, l++) {
        LaneObj *o = l->objs;

        l->tick_cnt++;
        if (l->tick_cnt < l->tick_div) continue;
        l->tick_cnt = 0;

        for (j = 0; j < l->num_objs; j++, o++) {
            if (l->dir == DIR_RIGHT) o->x16 += l->speed16;
            else o->x16 -= l->speed16;

            /* Wrap around */
            if (l->dir == DIR_RIGHT && o->x16 > X16(SCREEN_W + 16)) {
                o->x16 = -X16(o->width);
            } else if (l->dir == DIR_LEFT && o->x16 + X16(o->width) < -X16(16)) {
                o->x16 = X16(SCREEN_W);
            }
        }
    }
}

#endif

/* port (speed, same results): pointer walks instead of indexing, and the
   direction as add/subtract (z88dk's zsdcc turns both into multiply calls) */
int lanes_check_car(int px, int row)
{
    int i, j;
    Lane *l = lanes;

    for (i = 0; i < NUM_LANES; i++, l++) {
        LaneObj *o = l->objs;
        if (l->is_river) continue;
        if (l->row != row) continue;

        for (j = 0; j < l->num_objs; j++, o++) {
            int ox = lane_obj_x(o);
            int ow = o->width;

            /* AABB overlap with small inset for forgiving collisions */
            if (px + TILE_W - 5 > ox + 2 && px + 5 < ox + ow - 2) {
                return 1;
            }
        }
    }
    return 0;
}

int lanes_check_river(int px, int row, int *ride_dx16)
{
    int i, j;

    *ride_dx16 = 0;

    /* Only check river rows */
    if (row < ROW_RIVER_5 || row > ROW_RIVER_1) {
        return 1; /* not in river, safe */
    }

    Lane *l = lanes;
    for (i = 0; i < NUM_LANES; i++, l++) {
        LaneObj *o = l->objs;
        if (!l->is_river) continue;
        if (l->row != row) continue;

        for (j = 0; j < l->num_objs; j++, o++) {
            int ox = lane_obj_x(o);
            int ow = o->width;

            /* Match rendering guard: skip objects not currently drawn */
            if (ox < 0 || ox + ow > SCREEN_W) continue;

            if (px + TILE_W - 4 > ox && px + 4 < ox + ow) {
                /* Riding speed: only applies on movement frames */
                if (l->tick_cnt == 0) {
                    *ride_dx16 = l->dir == DIR_RIGHT ? l->speed16 : -l->speed16;   /* port */
                }
                return 1; /* on a log/raft */
            }
        }
    }

    return 0; /* in water, not on anything = death */
}

int lanes_check_home(int px)
{
    int slot;

    for (slot = 0; slot < NUM_HOMES; slot++) {
        int cx = home_cx[slot];
        int pad_w = 32;  /* generous landing zone */

        if (px + TILE_W / 2 >= cx - pad_w / 2 &&
            px + TILE_W / 2 <= cx + pad_w / 2) {
            if (home_filled[slot]) return -1; /* already filled */
            return slot;
        }
    }
    return -1; /* missed the slot */
}

void lanes_fill_home(int slot)
{
    if (slot >= 0 && slot < NUM_HOMES) {
        home_filled[slot] = 1;
    }
}

int lanes_all_home(void)
{
    int i;
    for (i = 0; i < NUM_HOMES; i++) {
        if (!home_filled[i]) return 0;
    }
    return 1;
}

void lanes_reset_home(void)
{
    int i;
    for (i = 0; i < NUM_HOMES; i++) {
        home_filled[i] = 0;
    }
}
