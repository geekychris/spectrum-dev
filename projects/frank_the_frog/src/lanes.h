/* Frank the Frog - lane objects (traffic + river). Port of lanes.h: drawing
   moved to render.c, rows passed instead of pixel y (rows differ in height). */
#ifndef LANES_H
#define LANES_H
#include "game.h"

typedef struct {
    WORD x16;       /* port: x in 1/16 pixel (can be negative for wrap) */
    WORD width;     /* width in pixels */
} LaneObj;

typedef struct {
    WORD row;       /* screen row */
    WORD dir;       /* DIR_LEFT or DIR_RIGHT */
    WORD speed16;   /* port: 1/16 pixels per tick (the original's px x 0.8) */
    WORD tick_div;  /* move every N frames */
    WORD tick_cnt;  /* frame counter */
    WORD num_objs;
    UBYTE kind;     /* port: what it is (for drawing): see KIND_* */
    WORD is_river;  /* 1 = river (logs/rafts), 0 = road (cars/buses) */
    LaneObj objs[MAX_LANE_OBJECTS];
} Lane;

enum { KIND_CAR_RED, KIND_CAR_YELLOW, KIND_CAR_BLUE, KIND_BUS, KIND_LOG, KIND_PADS };

#define NUM_LANES 10
extern Lane lanes[NUM_LANES];
extern int home_filled[NUM_HOMES];

void lanes_init(int level);
void lanes_tick(void);
int lanes_check_car(int px, int row);
int lanes_check_river(int px, int row, int *ride_dx16);
int lanes_check_home(int px);
int lanes_home_cx(int slot);
void lanes_fill_home(int slot);
int lanes_all_home(void);
void lanes_reset_home(void);
#define lane_obj_x(o) ((o)->x16 >> 4)     /* port: pixel x */
#endif
