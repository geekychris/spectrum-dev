// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ld.h - Lander 3D's simulation, ported from the browser game
 * (geekychris.github.io/tools/lander-3d, lander-3d-core.js): seeded terrain
 * (fBm hills, craters, flattened pads with score multipliers), a lander
 * with a main engine and RCS pitch and roll, three worlds (the Moon; Mars
 * with wind; a small asteroid), the touchdown check, and the autopilot
 * that flies the title demo and proves every level can be landed.
 *
 * Fixed point (16.16) throughout, so it runs on a 68020 without an FPU:
 * metres, m/s and radians are `fix`. The step is 1/100 s (four a frame).
 * No Amiga headers: tested on the host too.
 *
 * port: the Amiga original (tools/orig/ld.h) on the SDK's fix16.h, the
 * amiga3d engine's fixed point bit for bit. The ground, the pads, the start
 * and the tank of every level are made on the host by the original's own
 * code (tools/gen_levels.c, lv_rec.h): the terrain's heights stay in memory
 * pages (ter_*), not in the Level, and ld_new_level() reads a level's
 * record. `int` is 16 bits here; where the original's 32-bit int would
 * overflow, the expression is widened (marked port:). tools/check_core.sh
 * and the game's self-test (test.py) check the results against the
 * original's.
 */
#ifndef LD_H
#define LD_H

#include <stdint.h>
#include "fix16.h"                      /* port: the SDK's amiga3d fixed point */
#define FMUL(a, b) fmul((a), (b))
#define FDIV(a, b) fdiv((a), (b))

#define LD_STEP_HZ  100
/* port: x / 100 (truncated toward 0), fastdiv.asm on the Z80 */
#if defined(__SDCC)
extern int32_t div100(int32_t x) __z88dk_fastcall;
#else
static inline int32_t div100(int32_t x) { return x / 100; }
#endif
#define LD_DT       (FONE / LD_STEP_HZ)
#define LD_MAX_N    97

enum { W_MOON, W_MARS, W_ASTEROID, W_COUNT };
enum { OUT_NONE, OUT_LANDED, OUT_OFFPAD, OUT_CRASHED, OUT_LOST };

typedef struct {
    const char *name;
    fix g, amax;                        /* gravity, the engine's acceleration at full throttle */
    int size, n;                        /* metres across, grid points a side */
    fix hills, rough;
    int craters, crater_max;
    fix wind, drag, curve, dunes;
    fix lim_vs, lim_hs, lim_tilt;       /* landing limits: m/s, m/s, radians */
    fix fuel;                           /* seconds of full throttle */
} LdWorld;

extern const LdWorld ld_worlds[W_COUNT];
extern const char *const ld_outcome_names[5];

typedef struct { fix x, z, r, y; int mult; } Pad;

typedef struct {
    int world, n, size;
    fix cell, half, inv_cell;
    /* port: the heights are in memory pages (ter_*): hbase + 3 bytes each,
       rpp rows a page from `page`, rows of rowb bytes; hmax the highest */
    fix hmax, hbase;
    uint8_t page, rpp, range_page;       /* range_page: each row's highest, lowest (ter_range) */
    uint16_t rowb;
    Pad pads[3];
} Terrain;

typedef struct {
    fix fuel, start_alt, start_h, start_dist, pad_scale, wind;
} LevelParams;

typedef struct {
    fix p[3], v[3];
    fix pitch, roll, wp, wr;            /* attitude (rad), rates (rad/s) */
    fix throttle;
    int rcs[2];
} Craft;

typedef struct { fix throttle, pitch, roll; } LdInput;     /* 0..1, -1..1, -1..1 */

typedef struct {
    int vs_ok, hs_ok, tilt_ok, pad;
    fix vs, hs, tilt, slope;
    int32_t score, soft, fuel_bonus;
} LdResult;

/* events for sound and the log */
enum { LDE_NONE, LDE_LANDED, LDE_OFFPAD, LDE_CRASHED, LDE_LOST, LDE_FUEL_OUT, LDE_COUNT };

typedef struct {
    int world, level;
    uint32_t seed;
    LevelParams params;
    Terrain T;
    Craft c;
    fix fuel, fuel_max;
    int32_t t;                          /* steps */
    int outcome;
    LdResult result;
    fix wind_dir, wind_ph, wind[2];
    int assist;
    int ap_down;                        /* autopilot: descending over the pad */
    int ap_target;                      /* autopilot: the pad, -1 none */
    fix probe_used;                     /* fuel the autopilot's probe flight used */
    int probe_outcome;
    int events[8], nev;
} Level;

/* port: a level from the records made on the host (lv_rec.h): world 0..2,
   level 1.. (from LV_PER_WORLD on, round the last two) */
void ld_new_level(Level *L, int world, int level);
fix  ld_height(const Terrain *T, fix x, fix z);
fix  ld_slope_cos(const Terrain *T, fix x, fix z);      /* cos of the slope (1 = level) */
void ld_step(Level *L, const LdInput *in);
void ld_autopilot(Level *L, LdInput *out);
void ld_up(const Craft *c, fix u[3]);
void ld_feet(const Craft *c, fix out[5][3]);           /* the four feet and the engine bell */
fix  ld_tilt(const Craft *c);                           /* radians from vertical */
fix  ld_agl(const Level *L);                            /* height of the feet above the ground */
int  ld_nearest_pad(const Level *L, int easiest);
int  ld_pop_event(Level *L);

/* port: the terrain's heights (ter.c on the Next: the level's memory pages
   through MMU slot 2; an array on the host) */
fix  ter_h(const Terrain *T, uint8_t i, uint8_t j);    /* grid point (i, j) */
void ter_cell(const Terrain *T, uint8_t i, uint8_t j, fix h[4]);   /* (i, j) (i+1, j) (i, j+1) (i+1, j+1) */
void ter_done(void);                                    /* slot 2 back to the tilemap */
void ter_range(const Terrain *T, uint8_t j, int16_t *hi, int16_t *lo);   /* row j's heights in units */
/* the records (lv_rec.h): field f of record r */
int32_t lv_field(uint8_t r, uint8_t f);

#endif
