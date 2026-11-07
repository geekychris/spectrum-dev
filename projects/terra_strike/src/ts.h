// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ts.h - Terra Strike portable core: maths, terrain, world simulation.
 *
 * Nothing in here includes an Amiga header, so the same code is unit
 * tested on the host (make host-test) and on the Amiga (terra_strike_test
 * and the in-game "selftest" hook). Amiga-only code (screen, blitter,
 * audio, bridge) lives in main.c, render.c and sound.c.
 *
 * port (Spectrum Next): struct fields the original declares `int` (32 bits
 * on the Amiga) are int16_t, so the Next (16-bit int) and a host build of
 * the same code (tools/hostsim.c, tests/) share one layout, packed on the
 * host; their values fit. The 64 x 64 heightmap isn't in World: it is an
 * 8K memory page of its own (TS_HEIGHTS), made on the host by the
 * original's terrain_generate() (tools/gen_terrain.c) and loaded with the
 * game.
 *
 * Units: the world is 16384 x 16384 units and wraps in X and Z. Entity
 * positions are stored in "fine" units (world << FBITS) so slow movement
 * and gravity have sub-unit precision. Angles are 0..255 for a full turn;
 * yaw 0 faces +Z and yaw increases turning right (towards +X).
 */
#ifndef TS_H
#define TS_H

#include <stdint.h>

#ifndef __SDCC
#pragma pack(push, 1)    /* port: the host build shares the Next's layout */
#endif

/* ---------------- maths ---------------- */
#define FBITS          4
#define WORLD_BITS    14
#define WORLD_SIZE    (1L << WORLD_BITS)
#define WORLD_MASK    (WORLD_SIZE - 1)

extern const int32_t ts_sin_tab[256];          /* sin * 65536 */
#define TS_SIN(a) ts_sin_tab[(a) & 255]
#define TS_COS(a) ts_sin_tab[((a) + 64) & 255]

/* Shortest signed difference a-b on the wrapping world, in world units. */
int32_t ts_wrap_delta(int32_t a, int32_t b);
int32_t ts_wrap(int32_t a);                    /* into 0..WORLD_SIZE-1 */
uint32_t ts_rand(uint32_t *state);             /* 16-bit LCG output */
int ts_atan2(int32_t dx, int32_t dz);          /* angle of (dx,dz), 0..255 */

typedef struct { int32_t x, y, z; } V3;

/* Chase camera. Positions in world units. */
typedef struct {
    int32_t x, y, z;
    int16_t yaw, pitch;                 /* pitch > 0 looks down */
    int32_t cy, sy, cp, sp;         /* cached trig, 16.16 */
} Cam;

#define VIEW_W      320
#define VIEW_H      212             /* 3D area; HUD below */
#define VIEW_CX     160
#define VIEW_CY     96
#define FOV         170
#define NEAR_Z      24
#define FAR_Z       3600

void cam_setup(Cam *c);
void cam_xform(const Cam *c, int32_t wx, int32_t wy, int32_t wz, V3 *out);
/* Project a camera-space point (z >= NEAR_Z) to screen. */
void cam_project(const V3 *p, int32_t *sx, int32_t *sy);

/* Polygon clipping. Return the new vertex count (0 = invisible). */
#define MAX_PV 10
int clip_near(V3 *in, int n, V3 *out);
int clip_rect(int32_t *xs, int32_t *ys, int n, int32_t x0, int32_t y0,
              int32_t x1, int32_t y1);

/* Convex polygon -> horizontal spans (for the CPU filler). */
#define SPAN_MAX 256
int poly_spans(const int16_t *xs, const int16_t *ys, int n,
               int16_t *xl, int16_t *xr, int *ymin);

/* ---------------- terrain ---------------- */
#define MAP_BITS    6
#define MAP_N       (1 << MAP_BITS)            /* 64 x 64 vertices */
#define TILE_BITS   (WORLD_BITS - MAP_BITS)    /* 256 units per tile */
#define TILE        (1 << TILE_BITS)
#define SEA_LEVEL   160
#define MAX_HEIGHT  1100

enum { TER_SEA, TER_SAND, TER_GRASS, TER_FOREST, TER_ROCK, TER_SNOW };

/* ---------------- world ---------------- */
enum { ENT_NONE, ENT_TANK, ENT_BUILDING, ENT_SHIP, ENT_TREE };
enum { GS_TITLE, GS_PLAY, GS_LEVELCLEAR, GS_GAMEOVER };
enum { PS_LANDED, PS_FLYING, PS_DEAD };

/* Input bits (port: TSI_*, the original's IN_*: the SDK's zxgame.h has IN_*) */
#define TSI_LEFT    0x0001
#define TSI_RIGHT   0x0002
#define TSI_UP      0x0004      /* climb / lift off */
#define TSI_DOWN    0x0008      /* dive */
#define TSI_FIRE    0x0010
#define TSI_FAST    0x0020      /* throttle up */
#define TSI_SLOW    0x0040      /* throttle down */
#define TSI_ESC     0x0080
#define TSI_START   0x0100
#define TSI_PAUSE   0x0200

/* Events the simulation reports to the platform layer (sound, logging) */
enum { EV_NONE, EV_TAKEOFF, EV_LANDED, EV_FIRE, EV_KILL, EV_HIT_PLAYER,
       EV_CRASH, EV_LEVEL_CLEAR, EV_GAME_OVER, EV_ENEMY_FIRE, EV_RESPAWN,
       EV_LOW_FUEL };
typedef struct { int16_t type, a, b; } Event;

/* particle colours (palette pens, see models.h): base + 0/1 */
#define PART_RED   16
#define PART_FIRE  18
#define PART_WATER 4
#define MAX_EVENTS 16

#define MAX_ENTS      48
#define MAX_SHOTS     16
#define MAX_SHELLS    12
#define MAX_PARTS     96

typedef struct {
    int16_t type, hp, yaw, timer;
    int32_t x, y, z;                /* fine units, y = base on the ground */
} Ent;

typedef struct {
    int16_t active, life;
    int32_t x, y, z, vx, vy, vz;    /* fine */
} Shot;

typedef struct {
    int16_t life, col;
    int32_t x, y, z, vx, vy, vz;    /* fine */
} Part;

typedef struct {
    int16_t state;
    int32_t x, y, z;                /* fine */
    int32_t vy;                     /* fine / frame */
    int32_t speed;                  /* fine / frame, along yaw */
    int16_t yaw, bank;
    int16_t fuel, shield, cooldown, timer;
} Craft;

/* Tunables, exposed over the bridge so a host can retune a live game. */
typedef struct {
    int32_t god_mode;       /* 1 = shells and crashes do no damage */
    int32_t inf_fuel;
    int32_t enemy_fire;     /* frames between enemy shots (per enemy) */
    int32_t enemy_range;    /* world units */
    int32_t turn_rate;      /* yaw units / frame */
    int32_t lift;           /* fine / frame^2 */
    int32_t gravity;
    int32_t max_speed;      /* fine / frame */
    int32_t start_lives;
} Tune;

#ifdef __SDCC
#define TS_HEIGHTS ((int16_t *)0x6000)  /* port: MMU slot 3, the heightmap's page */
#else
extern int16_t ts_heights[MAP_N * MAP_N];
#define TS_HEIGHTS ts_heights
#endif

typedef struct {
    int16_t pad_tx, pad_tz;             /* landing pad: 2x2 tiles from here */
    int32_t pad_h;
    uint32_t seed;

    int16_t state, level, lives, frame, state_timer;
    int32_t score;
    int16_t targets_left;
    Craft p;
    Ent ents[MAX_ENTS];
    Shot shots[MAX_SHOTS];
    Shot shells[MAX_SHELLS];
    Part parts[MAX_PARTS];
    Event ev[MAX_EVENTS];
    int16_t nev;
    uint32_t rng;
    uint16_t last_in;
    Tune tune;
} World;

#ifndef __SDCC
#pragma pack(pop)
#endif

/* terrain.c */
void terrain_generate(World *w, uint32_t seed);   /* host only */
void terrain_island(World *w);                    /* port: the Next's pre-made island */
int32_t terrain_vertex(const World *w, int tx, int tz);   /* wraps */
int32_t terrain_height(const World *w, int32_t x, int32_t z); /* world units, bilinear, raw */
int32_t ground_height(const World *w, int32_t x, int32_t z);  /* max(raw, sea) */
int terrain_type(int32_t h);
int on_pad(const World *w, int32_t x, int32_t z);
void pad_centre(const World *w, int32_t *x, int32_t *z);

/* world.c */
void world_defaults(Tune *t);
void world_init(World *w, uint32_t seed);      /* title state */
void world_start_game(World *w);
void world_start_level(World *w, int level);
void world_update(World *w, unsigned input);
int  world_spawn(World *w, int type, int32_t x, int32_t z, int yaw); /* world units */
void world_clear_enemies(World *w);
int  world_count_targets(const World *w);
void world_place_player(World *w, int32_t x, int32_t y, int32_t z, int yaw, int flying);
int  world_pop_event(World *w, Event *e);
void world_explode(World *w, int32_t x, int32_t y, int32_t z, int n, int col);
int  ent_radius(int type);
int  ent_height(int type);
int  ent_points(int type);
const char *ent_name(int type);
/* "pad", "landing", "lowfuel", "sea" (ships), "tanks", "city" (buildings) */
int  world_scene(World *w, const char *name);

/* Radar: map an offset from the player to radar pixels (forward = up). */
void radar_map(int32_t dx, int32_t dz, int yaw, int scale_shift,
               int *rx, int *ry);

/* tests/test_cases.c */
typedef void (*ts_assert_fn)(int cond, const char *name);
int ts_run_tests(ts_assert_fn check);

#endif
