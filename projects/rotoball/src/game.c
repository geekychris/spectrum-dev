/*
 * Rotoball's rules, physics and CPU player: the original's main.c
 * (kickoff, check_goal, update_skimmer, resolve_collision, update_ball and
 * the CPU strategy in the main loop), in the same 16.16 arithmetic.
 *
 * port: the match clock counts logic steps (50 a second, the main loop's
 * fixed timestep) instead of reading the real-time clock; the original
 * also steps the game once per 1/50 s frame. A two-player mode drives P2
 * from the second player's controls instead of the CPU.
 */
#include <stdint.h>
#include "roto.h"
#include "fixmath.h"

Game G;
static uint8_t probe_events;           /* EV_* since the last probe_update */

/* the original's integer sine (Bhaskara), 16.16; the skimmers only ever
   face 0 or 180 degrees, so the right angles are answered at once */
int32_t roto_sin(int16_t d)
{
    int32_t t, s = 1;
    d %= 360;
    if (d < 0) d += 360;
    if (d == 0 || d == 180) return 0;
    if (d == 90) return ONE;
    if (d == 270) return -ONE;
    if (d >= 180) { d -= 180; s = -1; }
    t = (int32_t)d * (180 - d);
    return s * ((4 * t * ONE) / (40500 - t));
}

int32_t roto_cos(int16_t d) { return roto_sin(d + 90); }

static void kickoff(void)
{
    Skimmer *a = &G.p[0], *b = &G.p[1];
    a->x = -PITCH_LENGTH * 3 / 4; a->z = 0;
    a->vx = 0; a->vz = 0; a->angle = 0; a->has_ball = 0;
    b->x = PITCH_LENGTH * 3 / 4; b->z = 0;
    b->vx = 0; b->vz = 0; b->angle = 180; b->has_ball = 0;
    G.ball.x = 0; G.ball.z = 0; G.ball.vx = 0; G.ball.vz = 0; G.ball.carrier = -1;
}

void game_init(void)
{
    G.state = MS_TITLE;
    G.players = 1;
    G.score[0] = G.score[1] = 0;
    G.frames_left = MATCH_FRAMES;
    kickoff();
}

void game_start(uint8_t players)
{
    G.players = players;
    G.score[0] = G.score[1] = 0;
    G.frames_left = MATCH_FRAMES;
    G.last_scorer = -1;
    G.tackle_cooldown = 0;
    G.tackle_lock = 0;
    G.steps = 0;
    G.state = MS_PLAYING;
    kickoff();
}

/* the ball over a goal line: points by how far out the carrier was (the
   carried ball is 2 units ahead, so in practice a goal is worth 1, as in
   the original) */
static void check_goal(void)
{
    int8_t c = G.ball.carrier;
    int32_t dist;
    uint8_t pts;
    if (G.ball.x >= PITCH_LENGTH) {
        pts = 1;
        if (c == 0) {
            dist = PITCH_LENGTH - G.p[0].x;
            pts = (dist > PITCH_LENGTH * 3 / 4) ? 3 : (dist > PITCH_LENGTH / 2) ? 2 : 1;
        }
        G.score[0] += pts;
        G.last_scorer = 0;
        G.last_pts = pts;
        G.events |= EV_GOAL1;
        kickoff();
    } else if (G.ball.x <= -PITCH_LENGTH) {
        pts = 1;
        if (c == 1) {
            dist = G.p[1].x + PITCH_LENGTH;
            pts = (dist > PITCH_LENGTH * 3 / 4) ? 3 : (dist > PITCH_LENGTH / 2) ? 2 : 1;
        }
        G.score[1] += pts;
        G.last_scorer = 1;
        G.last_pts = pts;
        G.events |= EV_GOAL2;
        kickoff();
    }
}

/* skimmer physics: one Euler step (the original's update_skimmer) */
#define ACCEL       ((int32_t)(ONE / 20))
#define STRAFE      ((int32_t)(ONE / 25))
#define FRICTION_N  15
#define FRICTION_D  16
#define V_CAP       (ONE * 2)

static void update_skimmer(Skimmer *r, uint8_t c)
{
    int32_t ca = roto_cos(r->angle), sa = roto_sin(r->angle);
    int32_t af = 0, ar = 0, ax, az, mag2;
    if (c & C_FWD) af += ACCEL;
    if (c & C_BACK) af -= ACCEL;
    if (c & C_RIGHT) ar += STRAFE;
    if (c & C_LEFT) ar -= STRAFE;
    ax = ((af >> 8) * (ca >> 8) + (ar >> 8) * (sa >> 8));
    az = ((af >> 8) * (sa >> 8) - (ar >> 8) * (ca >> 8));
    r->vx += ax;
    r->vz += az;

    r->vx = (r->vx * FRICTION_N) / FRICTION_D;
    r->vz = (r->vz * FRICTION_N) / FRICTION_D;

    mag2 = ((r->vx >> 8) * (r->vx >> 8) + (r->vz >> 8) * (r->vz >> 8));
    if (mag2 > (V_CAP >> 8) * (V_CAP >> 8)) {
        r->vx = r->vx * 3 / 4;
        r->vz = r->vz * 3 / 4;
    }

    r->x += r->vx;
    r->z += r->vz;

    if (r->x > PITCH_LENGTH) { r->x = PITCH_LENGTH; r->vx = 0; }
    if (r->x < -PITCH_LENGTH) { r->x = -PITCH_LENGTH; r->vx = 0; }
    if (r->z > PITCH_WIDTH) { r->z = PITCH_WIDTH; r->vz = 0; }
    if (r->z < -PITCH_WIDTH) { r->z = -PITCH_WIDTH; r->vz = 0; }
}

#define BALL_FRICTION_N  30
#define BALL_FRICTION_D  32
#define PICKUP_DIST      (3 * ONE)
#define ROTO_RADIUS      (2 * ONE)
#define TACKLE_COOLDOWN_F 60

/* skimmer against skimmer: bump apart, and a tackle takes the ball (the
   original's resolve_collision) */
static void resolve_collision(Skimmer *a, Skimmer *b)
{
    int32_t dx = b->x - a->x, dz = b->z - a->z;
    int32_t sep2 = ((dx >> 8) * (dx >> 8) + (dz >> 8) * (dz >> 8));
    int32_t min = (ROTO_RADIUS * 2) >> 8;
    int32_t dist, nx, nz, overlap, push_x, push_z, rel_vx, rel_vz, rel_vn, imp_x, imp_z;
    if (sep2 >= min * min) { G.tackle_lock = 0; return; }   /* apart: tackles re-armed */

    if (G.tackle_cooldown == 0 && !G.tackle_lock && G.ball.carrier >= 0) {
        uint8_t steal_to = (G.ball.carrier == 0) ? 1 : 0;
        Skimmer *nc = steal_to == 0 ? a : b;
        Skimmer *oc = steal_to == 0 ? b : a;
        int32_t kca = roto_cos(nc->angle), ksa = roto_sin(nc->angle);
        oc->has_ball = 0;
        nc->has_ball = 1;
        G.ball.carrier = steal_to;
        G.tackle_cooldown = TACKLE_COOLDOWN_F;
        G.tackle_lock = 1;
        G.events |= EV_TACKLE;
        nc->vx += kca;
        nc->vz += ksa;
        oc->vx -= kca / 2;
        oc->vz -= ksa / 2;
    }

    /* port: the original counts dist up from 0 to sqrt(sep2) (up to 1023
       steps); isqrt32 gives the same floor(sqrt) */
    dist = isqrt32((uint32_t)sep2);
    if (dist < 1) dist = 1;

    nx = ((dx >> 8) * 256) / dist;
    nz = ((dz >> 8) * 256) / dist;
    overlap = min - dist;
    push_x = (overlap * nx) >> 1;
    push_z = (overlap * nz) >> 1;
    a->x -= push_x;
    a->z -= push_z;
    b->x += push_x;
    b->z += push_z;

    rel_vx = b->vx - a->vx;
    rel_vz = b->vz - a->vz;
    rel_vn = ((rel_vx >> 8) * nx + (rel_vz >> 8) * nz);
    if (rel_vn >= 0) return;
    imp_x = (-rel_vn * nx) >> 8;
    imp_z = (-rel_vn * nz) >> 8;
    a->vx -= imp_x >> 1;
    a->vz -= imp_z >> 1;
    b->vx += imp_x >> 1;
    b->vz += imp_z >> 1;
}

static void update_ball(void)
{
    Ball *bl = &G.ball;
    if (bl->carrier < 0) {
        int32_t d1x = bl->x - G.p[0].x, d1z = bl->z - G.p[0].z;
        int32_t d2x = bl->x - G.p[1].x, d2z = bl->z - G.p[1].z;
        int32_t r2 = ((PICKUP_DIST >> 8) * (PICKUP_DIST >> 8));
        int32_t dd1 = ((d1x >> 8) * (d1x >> 8) + (d1z >> 8) * (d1z >> 8));
        int32_t dd2 = ((d2x >> 8) * (d2x >> 8) + (d2z >> 8) * (d2z >> 8));
        if (dd1 < r2 && dd1 <= dd2) { bl->carrier = 0; G.p[0].has_ball = 1; G.events |= EV_PICKUP; }
        else if (dd2 < r2) { bl->carrier = 1; G.p[1].has_ball = 1; G.events |= EV_PICKUP; }
        bl->x += bl->vx;
        bl->z += bl->vz;
        bl->vx = (bl->vx * BALL_FRICTION_N) / BALL_FRICTION_D;
        bl->vz = (bl->vz * BALL_FRICTION_N) / BALL_FRICTION_D;
    } else {
        Skimmer *c = &G.p[bl->carrier];
        bl->x = c->x + 2L * roto_cos(c->angle);
        bl->z = c->z + 2L * roto_sin(c->angle);
        bl->vx = 0; bl->vz = 0;
    }
    if (bl->z > PITCH_WIDTH) { bl->z = PITCH_WIDTH; bl->vz = -bl->vz; }
    if (bl->z < -PITCH_WIDTH) { bl->z = -PITCH_WIDTH; bl->vz = -bl->vz; }
}

/* the original's CPU: chase the ball, or carry it to P1's goal; P2 faces
   -X, so "forward" is -X and "right" is +Z */
void cpu_controls(uint8_t *c)
{
    Skimmer *me = &G.p[1];
    int32_t tx, tz, dx, dz;
    if (me->has_ball) { tx = -PITCH_LENGTH; tz = 0; }
    else { tx = G.ball.x; tz = G.ball.z; }
    dx = tx - me->x;
    dz = tz - me->z;
    *c = 0;
    if (dx < -ONE) *c |= C_FWD;
    if (dx > ONE) *c |= C_BACK;
    if (dz > ONE) *c |= C_RIGHT;
    if (dz < -ONE) *c |= C_LEFT;
}

void game_step(uint8_t c1, uint8_t c2)
{
    G.events = 0;
    if (G.state != MS_PLAYING) return;
    if (G.players == 1) cpu_controls(&c2);    /* decided before anything moves, as in the original */
    update_skimmer(&G.p[0], c1);
    update_skimmer(&G.p[1], c2);
    resolve_collision(&G.p[0], &G.p[1]);
    update_ball();
    check_goal();
    if (G.tackle_cooldown > 0) G.tackle_cooldown--;
    G.steps++;
    if (--G.frames_left <= 0) {
        G.frames_left = 0;
        G.state = MS_FULLTIME;
        G.events |= EV_FULL;
    }
    probe_events |= G.events;
}

/* the bot's view (see roto.h) */
uint8_t roto_probe[PROBE_SIZE];

static uint8_t *put32(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    return p + 4;
}

void probe_update(void)
{
    uint8_t *p = roto_probe;
    p = put32(p, G.p[0].x); p = put32(p, G.p[0].z);
    p = put32(p, G.p[1].x); p = put32(p, G.p[1].z);
    p = put32(p, G.ball.x); p = put32(p, G.ball.z);
    *p++ = (uint8_t)G.ball.carrier;
    *p++ = (uint8_t)G.score[0]; *p++ = (uint8_t)(G.score[0] >> 8);
    *p++ = (uint8_t)G.score[1]; *p++ = (uint8_t)(G.score[1] >> 8);
    p = put32(p, G.frames_left);
    *p++ = G.state;
    *p++ = G.players;
    *p++ = G.tackle_cooldown;
    *p = probe_events;
    probe_events = 0;
}
