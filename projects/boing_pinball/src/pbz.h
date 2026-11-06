/*
 * pbz.h - Boing Pinball's simulation for the Z80 (Spectrum Next).
 *
 * The original (pb.h, pb_core.c) is 16.16 fixed point: 61K of zsdcc code
 * and far too slow at 120 steps a second. This is the same simulation,
 * function for function, in 16-bit units:
 *
 *   P  position, 1/16 mm (int16: -2048..2047 mm); balls keep an 8-bit fraction
 *   V  velocity, 1/4 mm/s (int16: up to 8191 mm/s; the cap is 7000)
 *   Q14 unit vectors and other 0..1 factors (16384 = 1.0)
 *   A  angle, 1/4096 radian;  W  angular speed, 1/256 rad/s
 *
 * The tables are built on the host by the original pb_tables.c and
 * converted (tools/gen_table.py -> table_*.c); the rules (pb_rules.c)
 * are the original's.
 */
#ifndef PBZ_H
#define PBZ_H

#include <stdint.h>

#define PB_HZ        120
#define PB_SECS(s)   ((int16_t)((s) * PB_HZ))      /* seconds -> steps */

#define P_MM(v)      ((int16_t)((v) * 16))          /* mm -> P */
#define V_MM(v)      ((int16_t)((v) * 4))           /* mm/s -> V */
#define Q14(v)       ((int16_t)((v) * 16384))

#define PB_R         216                /* 13.5 mm */
#define PB_GRAVITY_STEP 50              /* 1500 mm/s^2 for 1/120 s, in V */
#define PB_MAX_SPEED V_MM(7000)
#define PB_MID       P_MM(239)

#define PB_MAX_SEG   80
#define PB_MAX_CIRC  10
#define PB_MAX_SENS  12
#define PB_MAX_FLIP  3
#define PB_MAX_RPTS  16
#define PB_MAX_SPIN  2
#define PB_MAX_BALLS 4
#define PB_MAX_EV    24
#define PB_MAX_MSG   6
#define PB_MAX_TIMER 8

#define PB_GX        9
#define PB_GY        19

enum { SK_WALL, SK_OUTER, SK_SLING, SK_GATE, SK_FLOOR, SK_DROP, SK_GUIDE };

typedef struct {
    int16_t ax, ay, bx, by;             /* P */
    int16_t ux, uy;                     /* Q14 */
    int16_t len, r;                     /* P */
    int16_t bb[4];                      /* P: x0 y0 x1 y1, grown by r + the ball + 4 mm */
    uint8_t e;                          /* restitution, /256 */
    int8_t kind, id, ow_x, ow_y, bank;
    int8_t up;                          /* a drop target is standing */
} Seg;

typedef struct { int16_t cx, cy, r; uint8_t e; int8_t bumper, id; } Circ;

enum { SN_LANE, SN_IN, SN_OUT, SN_SPIN, SN_RAMPIN, SN_LANEOUT };
typedef struct { int16_t ax, ay, bx, by; int16_t bb[4]; int8_t kind, idx; } Sensor;

typedef struct { int8_t side; int16_t px, py, len, r1, r2, rest, up; } FlipDef;   /* P; A */

typedef struct {
    int8_t n;
    int16_t pts[PB_MAX_RPTS][3];        /* x, y, height (mm) */
    int16_t prof[PB_MAX_RPTS];          /* arc length to each point, P */
    int16_t exit_x, exit_y, exit_vy;    /* P, P, V */
} Ramp;

typedef struct { int16_t x, y; } SpinDef;

typedef struct {
    const char *name;
    const char *mission[5];
    int8_t nseg, ncirc, nsens, nflip, nramp, nspin, ndrop;
    Seg *seg;                           /* in RAM: drop targets go up and down */
    const Circ *circ;
    const Sensor *sens;
    const FlipDef *flip;
    const Ramp *ramp;
    const SpinDef *spin;
    const int8_t *drop_seg;             /* drop target i -> its index in seg[] */
    int16_t saucer_x, saucer_y, saucer_r, plunger_x, plunger_rest;   /* P */
    const uint16_t *grid_at;            /* [PB_GY * PB_GX + 1]: start of each cell's entries */
    const uint16_t *grid;               /* (type << 12) | index */
} Table;

/* ---------- the world ---------- */
enum { BS_PLAY, BS_LANE, BS_RAMP, BS_SAUCER, BS_DRAINED };

typedef struct {
    int8_t id, state, ramp;
    uint8_t xf, yf;                     /* position fractions, 1/256 P */
    int16_t x, y, vx, vy, z;            /* P, V; z in mm (on a ramp) */
    int16_t rs, rv;                     /* on a ramp: P along it, V */
    int16_t hold;                       /* in the saucer: steps left */
    int16_t nocatch;                    /* steps the saucer won't catch it */
    int16_t autol;                      /* steps to an automatic launch, -1 none */
    uint8_t spin;                       /* the Boing ball's turn (drawing) */
} Ball;

typedef struct { FlipDef d; int16_t ang, w; int8_t held; int16_t tx, ty, tip_ang; } Flipper;   /* A, W; the tip, cached */
typedef struct { int16_t ang, w; } Spinner;                           /* A (0..pi), W */

enum {
    EV_WALL, EV_SLING, EV_DROP, EV_BUMPER, EV_FLIPHIT, EV_FLIPSTOP, EV_SPINHIT, EV_SPIN,
    EV_SENSOR, EV_RAMPENTER, EV_RAMP, EV_RAMPFAIL, EV_SAUCER, EV_KICK, EV_LANEREST, EV_DRAIN, EV_PLUNGE,
    EV_SCORE, EV_SKILL, EV_LANESDONE, EV_BANK, EV_BANKRESET, EV_LOCK, EV_MULTIBALL, EV_JACKPOT,
    EV_MISSIONSTART, EV_MISSIONDONE, EV_MISSIONFAIL, EV_SUPERNOVA, EV_SAVE, EV_ENDBALL, EV_GAMEOVER,
    EV_TILTWARN, EV_TILT, EV_NUDGE, EV_OUTLANE, EV_COUNT
};

typedef struct {
    int8_t type, id, kind, up;
    int16_t x, y;                       /* mm */
    int16_t ball;
    int32_t val;                        /* speed (mm/s), points, pull (0..1000) */
} Event;

typedef struct {
    Table *t;
    Ball balls[PB_MAX_BALLS]; int8_t nballs;
    Flipper flip[PB_MAX_FLIP]; int8_t nflip;
    Spinner spin[PB_MAX_SPIN];
    Event ev[PB_MAX_EV]; int8_t nev;
    int8_t next_id;
    int8_t flipper_power, active_targets;
    int8_t substeps;
    uint16_t tests;
} World;

void pb_world_init(World *w, Table *t);
Ball *pb_add_ball(World *w, int16_t x, int16_t y, int16_t vx, int16_t vy, int8_t state);
void pb_world_step(World *w);           /* one 1/120 s step */
void pb_plunge(World *w, Ball *b, int16_t pull);     /* pull 0..1000 */
void pb_kick_saucer(World *w, Ball *b, int16_t jitter);   /* V, added to the kick across */
void pb_nudge(World *w, int16_t dx, int16_t dy);     /* V */
void pb_flipper_tip(const Flipper *f, int16_t *x, int16_t *y);
void pb_remove_drained(World *w);

/* ---------- the rules (pb_rules.c, the original's) ---------- */
enum { GS_ATTRACT, GS_PLAY, GS_BONUS, GS_OVER };
enum { MI_BUMPER, MI_BANK, MI_SPIN, MI_RAMP, MI_LANES };

typedef struct { const char *goal; int16_t need; int8_t ev; int32_t award; } MissionDef;
extern const MissionDef pb_missions[5];

typedef struct { int32_t until, at; char big[20], small[24]; } Msg;
typedef struct { int32_t at; int8_t kind, arg; } Timer;
enum { TM_BANK_RESET, TM_SERVE };

typedef struct {
    int8_t left, right, plunger;        /* held */
    int16_t pull_to;                    /* 0..1000 sets the plunger directly; -1 none */
    int8_t nudge_x, nudge_y;            /* -1/0/1 */
} PbInput;

typedef struct {
    World w;
    Table *t;
    int8_t state, balls_per_game, extra_balls;
    int32_t score, time;                /* time in steps */
    int8_t ball_no;
    int8_t lanes[3], skill_lane, skill_armed;
    int8_t bonus_x, locks;
    int16_t banks, ramps;
    int8_t missions_done;               /* bit per mission */
    int8_t mission, mission_lit; int16_t mission_n; int32_t mission_until;
    int8_t multiball; int16_t jackpot;
    int32_t supernova, save_until, plunge_at, jackpot_lit;
    int16_t bonus_ramps, bonus_targets, bonus_lanes, bonus_spins;
    int32_t bonus_pts; int16_t bonus_t;
    int16_t tilt_meter;                 /* 1/256 */
    int8_t tilt_warnings, tilted;
    int16_t pull; int8_t pull_held;     /* pull 0..1000 */
    Msg msg[PB_MAX_MSG]; int8_t nmsg;
    Timer timers[PB_MAX_TIMER]; int8_t ntimers;
    Event ev[PB_MAX_EV]; int8_t nev;    /* what happened, for the sound and the lamps */
    uint32_t rng;
} Game;

void pb_game_init(Game *g, Table *t, uint32_t seed);
void pb_game_start(Game *g);
void pb_game_tick(Game *g, const PbInput *in);       /* one step */
void pb_game_handle(Game *g, const Event *e);
void pb_say(Game *g, const char *big, const char *small, int32_t steps);
void pb_award(Game *g, int32_t pts, int16_t x, int16_t y);
void pb_mission_hit(Game *g, int8_t ev);
void pb_do_tilt(Game *g);
int8_t pb_live_balls(const Game *g);

typedef struct { const char *big, *small; int8_t flash; } Dmd;
void pb_dmd(Game *g, Dmd *d);
/* the lamps' order is the original's (L_LANE0 ...) */
enum {
    L_LANE0, L_LANE1, L_LANE2, L_LOCK0, L_LOCK1, L_LOCK2, L_JACKPOT,
    L_M0, L_M1, L_M2, L_M3, L_M4, L_MISSION, L_X2, L_X3, L_X4, L_X5,
    L_SAVE, L_SPIN0, L_SPIN1, L_SUPER, L_DROP0,
    L_COUNT = L_DROP0 + 8
};
void pb_lamps(const Game *g, int8_t blink, int8_t chase, uint8_t on[L_COUNT]);
void pb_fmt(char *out, int32_t n);

typedef struct { uint32_t rng; int16_t hold[2], launch; } Bot;
void pb_bot(const Game *g, PbInput *in, Bot *bot);

uint32_t a3_rand(uint32_t *state);      /* the original's 15-bit LCG */

/* ---------- maths (zmath.c / zmath.asm) ---------- */
int32_t mul16(int16_t a, int16_t b);                 /* a * b */
int16_t mulq14(int16_t a, int16_t b);                /* (a * b) >> 14 */
int16_t mulq8(int16_t a, int16_t b);                 /* (a * b) >> 8 */
uint16_t isqrt32(uint32_t v);
int16_t div_q14(int16_t a, int16_t d);               /* (a << 14) / d, |a| <= |d| */
int16_t sin_q14(int16_t a);                          /* A -> Q14 */
#define cos_q14(a) sin_q14((int16_t)((a) + 6434))     /* + pi/2 */

#endif
