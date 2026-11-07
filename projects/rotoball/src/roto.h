/*
 * Rotoball for the Spectrum Next: the game's state and rules (the
 * original's rotoball.h and the gameplay half of main.c).
 *
 * Coordinate system (the original's):
 *   +X = down the pitch towards P2's goal, -X towards P1's goal
 *   +Z = P1's left when facing +X (P1's "right" strafe goes to -Z, which
 *        the renderer draws to the right)
 * Coordinates are 16.16 fixed point (int32_t) as in the original.
 *
 * game.c is plain C: it also builds on the host (tools/sim.c) for the
 * bot's simulator, with the same results.
 */
#ifndef ROTO_H
#define ROTO_H
#include <stdint.h>

#define FP    16
#define ONE   (1L << FP)

#define PITCH_LENGTH   (100L * ONE)   /* goals at +/- 100 */
#define PITCH_WIDTH    ( 40L * ONE)   /* sidelines at +/- 40 */
#define HOVER_H        (  2L * ONE)
#define BALL_RADIUS    ( ONE)

/* match flow: title (the original's kick-off screen) -> playing -> full
   time (play frozen under the result) */
#define MS_TITLE     0
#define MS_PLAYING   1
#define MS_FULLTIME  2

#define CLOCK_HZ     50
#define MATCH_FRAMES (90L * CLOCK_HZ)  /* 90 seconds */

typedef struct {
    int32_t x, z;
    int32_t vx, vz;
    int16_t angle;              /* 0 = facing +X (P1), 180 = facing -X (P2) */
    uint8_t has_ball;
} Skimmer;

typedef struct {
    int32_t x, z;
    int32_t vx, vz;
    int8_t carrier;             /* -1 free, 0 P1, 1 P2 */
} Ball;

/* a player's controls for one step */
#define C_FWD   1
#define C_BACK  2
#define C_LEFT  4
#define C_RIGHT 8

/* events of the last step, for sound and the HUD (bits) */
#define EV_PICKUP  1
#define EV_TACKLE  2
#define EV_GOAL1   4            /* P1 scored */
#define EV_GOAL2   8            /* P2 scored */
#define EV_FULL    16           /* full time */

typedef struct {
    Skimmer p[2];               /* 0 = P1 (bottom pane), 1 = P2 / the CPU (top pane) */
    Ball ball;
    int16_t score[2];
    int32_t frames_left;
    int8_t last_scorer;
    uint8_t tackle_cooldown;
    uint8_t tackle_lock;
    uint8_t state;              /* MS_* */
    uint8_t players;            /* 1: P2 is the CPU; 2: two people */
    uint8_t events;             /* EV_* of the last step */
    uint8_t last_pts;           /* points of the last goal */
    uint16_t steps;             /* steps of this match */
} Game;

extern Game G;

void game_init(void);                       /* power on: the title */
void game_start(uint8_t players);           /* a new match */
/* one step (1/50 s) of play with P1's and P2's controls (P2's are ignored
   against the CPU) */
void game_step(uint8_t c1, uint8_t c2);
void cpu_controls(uint8_t *c);              /* the original's CPU for P2 */

/* sin / cos of the two facing angles, 16.16 (the original's table gives
   exactly +/-ONE and 0 there) */
int32_t roto_cos(int16_t deg);
int32_t roto_sin(int16_t deg);

/* the state for the test's bot, as bytes (little-endian, the same on the
   host): x, z of P1, P2 and the ball (6 x int32), carrier (int8), scores
   (2 x int16), frames left (int32), state, players, tackle cooldown, events
   of the steps since the last probe_update (uint8 each) */
#define PROBE_SIZE 37
extern uint8_t roto_probe[PROBE_SIZE];
void probe_update(void);

#endif
