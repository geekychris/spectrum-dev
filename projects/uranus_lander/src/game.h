/*
 * Uranus Lander - game definitions (port of amiga_mcp/examples/uranus_lander/game.h).
 *
 * The game keeps the original's 320x256 world and 16.16 fixed-point physics,
 * so terrain, pads, gravity, thrust, fuel and the landing rules are exactly
 * the Amiga's; the Spectrum front end (view.c) draws that world scaled to
 * 256x192. Port changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H

#include "amiga_types.h"

/* World constants (the original's screen) */
#define SCREEN_W    320
#define SCREEN_H    256

/* Fixed-point 16.16 */
typedef LONG Fixed;
#define FIX16(x)     ((Fixed)(x) << 16)       /* port: FIX is zxgame.h's 8.8 */
/* the original's FIX_INT (rounds). port: 0x8000L, not 0x8000: zsdcc made
   long + 0x8000 unsigned, so -0.7 rounded to 65535 ("below the screen") */
#define FIX_ROUND(x) ((WORD)(((x) + 0x8000L) >> 16))

/* Angle system: 256 steps, 0 = up */
#define ANGLE_COUNT 256

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_LANDED     2
#define STATE_CRASHING   3
#define STATE_GAMEOVER   4
#define STATE_ENTER_NAME 5

/* Colours of the original's palette (view.c maps them to the Spectrum's) */
#define COL_BG       0
#define COL_WHITE    1
#define COL_LTGRAY   4
#define COL_YELLOW   6
#define COL_RED      8
#define COL_ORANGE   9
#define COL_SLATE    14

/* Physics defaults (tunable) */
#define GRAVITY_DEFAULT      1000
#define THRUST_DEFAULT       3200
#define FUEL_DEFAULT         600
#define SAFE_VY_DEFAULT      55000
#define SAFE_VX_DEFAULT      35000
#define SAFE_ANGLE_DEFAULT   14
#define TURN_RATE_DEFAULT    2
#define START_LIVES_DEFAULT  3

/* Terrain */
#define TERRAIN_W    320
#define TERRAIN_MIN  130
#define TERRAIN_MAX  238

#define MAX_PADS      5
#define MAX_PARTICLES 32
#define MAX_STARS     60

typedef struct {
    Fixed x, y;
    Fixed vx, vy;
    WORD angle;        /* 0-255, 0 = up */
    WORD fuel;
    WORD alive;
    WORD thrusting;
} Ship;

typedef struct {
    WORD x;
    WORD width;
    WORD y;
    WORD multiplier;   /* 1, 2, 3, or 5 */
} LandingPad;

typedef struct {
    Fixed x, y;
    Fixed vx, vy;
    WORD life;
    WORD color;
    WORD active;
} Particle;

typedef struct {
    WORD x, y;
    WORD brightness;   /* colour index */
    WORD speed;        /* sub-pixel speed (1-3) */
    WORD accum;        /* sub-pixel accumulator */
} Star;

typedef struct {
    LONG gravity;
    LONG thrust;
    LONG fuel_max;
    LONG safe_vy;
    LONG safe_vx;
    LONG safe_angle;
    LONG turn_rate;
    LONG start_lives;
} Tunables;

typedef struct {
    WORD left;
    WORD right;
    WORD thrust;
    WORD up;
    WORD down;
    WORD quit;
} InputState;

typedef struct {
    Ship ship;
    LandingPad pads[MAX_PADS];
    WORD num_pads;
    WORD terrain_y[TERRAIN_W];
    Particle particles[MAX_PARTICLES];
    Star stars[MAX_STARS];

    LONG score;
    WORD lives;
    WORD level;
    WORD state;
    WORD state_timer;
    WORD frame;

    WORD landed_pad;
    LONG land_bonus;

    /* port: the high-score table and the name entry are the SDK's (hs_*) */

    /* sound event flags */
    WORD ev_thrust;
    WORD ev_crash;
    WORD ev_land;
    WORD ev_low_fuel;

    WORD particles_live;   /* port: particles active (update/draw skip none) */
} GameState;

/* port: sine in Q14 (fixmath.h's fix_sin14) instead of the original's 16.16
   table; thrust is (sin14 * thrust) >> 14 through mul16 */

extern Tunables g_tune;

void game_init(GameState *gs);
void game_new_level(GameState *gs);
void game_update(GameState *gs, InputState *inp);
void stars_update(GameState *gs, uint8_t frames);
WORD hiscore_qualifies(GameState *gs);

#endif /* GAME_H */
