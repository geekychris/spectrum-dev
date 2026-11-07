/*
 * Miner Meteor - the game rules, ported from amiga_mcp/examples/miner_meteor
 * (game.h / game.cpp). No drawing, no input, no sound: game_tick(input) once
 * per 50 Hz tick. Builds for the Spectrum and on the host (tools/solve.c).
 *
 * port: positions and speeds are int16 (the original's LONG); the grid's
 * kinds live in a tmap (sdk/include/tmap.h) and the player moves with
 * sdk plat.h, which is the original's update_player collision code
 * unchanged. Other port changes are marked `port:` in game.c.
 */
#ifndef MINER_GAME_H
#define MINER_GAME_H

#include <stdint.h>
#include "plat.h"

#define TILE_W          16
#define TILE_H          16
#define GRID_W          20
#define GRID_H          15
#define PLAY_W          (GRID_W * TILE_W)     /* 320 */
#define PLAY_H          (GRID_H * TILE_H)     /* 240 */

enum TileKind {
    T_EMPTY = 0, T_SOLID = 1, T_KEY = 2, T_EXIT = 3, T_COLLAPSE = 4,
    T_CONV_L = 5, T_CONV_R = 6, T_HAZARD = 7,
    T_SPAWN = 8,            /* 'P' in the level data; empty once loaded */
    T_KIND_COUNT
};

#define GRAVITY             1
#define MAX_FALL_SPEED      6
/* Bug in the original: -8 rose 36 px but the keys sit 64 px up (the
 * original was fixed to -12 too; see its README). */
#define JUMP_IMPULSE        (-12)
#define WALK_SPEED          2
#define CONVEYOR_PUSH       1
#define COLLAPSE_FRAMES     18

typedef struct {
    int16_t x, y, dx, dy, min_pos, max_pos;
    uint8_t axis, colour, alive, frame;
} Guardian;

#define MAX_GUARDIANS  6

typedef struct {
    plat_body b;            /* x, y, vx, vy, on_ground */
    uint8_t facing;         /* 0 = left, 1 = right */
    uint8_t frame;          /* walk cycle 0..3 */
    uint8_t jump_lock;
} Player;

enum GameMode { GM_TITLE = 0, GM_PLAYING = 1, GM_WIN = 2, GM_LOSE = 3, GM_LEVELWIN = 4 };

#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_JUMP   0x04
#define INPUT_START  0x08

/* port: collapsing tiles that are counting down, instead of a pass over
   all 300 cells each tick (same result: each timer is independent) */
#define MAX_FALLING 24
typedef struct { uint16_t cell; uint8_t timer; } Falling;

/* port: what the front end needs to know about a tick (sounds, redraws) */
#define EV_KEY      0x01
#define EV_DIED     0x02
#define EV_JUMP     0x04
#define EV_LEVEL    0x08    /* cavern cleared */
#define EV_LOADED   0x10    /* a cavern was (re)loaded: redraw it all */
#define EV_GAMEOVER 0x20
#define EV_WON      0x40
#define EV_UNLOCK   0x80    /* the last key: the exit opens */
#define MAX_CHANGED 8

typedef struct {
    uint8_t  mode;
    uint8_t  level_index;
    uint8_t  lives;
    uint8_t  keys_remaining;
    uint8_t  keys_total;
    uint32_t score;
    int16_t  air;               /* 0..1000 */
    int16_t  state_timer;

    Player   player;
    uint8_t  kind[GRID_H * GRID_W];
    Falling  falling[MAX_FALLING];
    uint8_t  num_falling;
    Guardian guards[MAX_GUARDIANS];
    uint8_t  num_guards;

    int16_t  spawn_x, spawn_y;
    uint16_t tick;              /* port: 16-bit (only tick % 6 is used) */
    uint8_t  phase6;            /* port: tick % 6 kept as a counter */
    uint16_t level_ticks;       /* port: ticks since the cavern was loaded (test bots) */

    uint8_t  events;            /* EV_* of the last tick */
    uint8_t  num_changed;       /* cells whose kind changed (keys, floors) */
    uint16_t changed[MAX_CHANGED];
} GameState;

#define AIR_DRAIN_FRAMES  6
#define AIR_MAX           1000
#define KEY_SCORE         100
#define LEVEL_SCORE       1000

extern GameState gs;
extern const uint8_t num_levels;

extern void game_init(void);
extern void game_load_level(uint8_t index);
extern void game_tick(uint8_t input);
extern uint8_t game_collapse_timer(uint16_t cell);   /* 0 = not falling */

/* levels.c: the guardians of each cavern (the original's tables) */
typedef struct {
    uint8_t axis, colour;
    int8_t  dx, dy;
    uint16_t x, y, min_pos, max_pos;
} GuardianSpec;
extern const GuardianSpec *const level_guards[];
extern const uint8_t level_palette[];

#endif
