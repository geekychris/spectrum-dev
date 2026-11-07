// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * od_core.c - Orbit Docking's rules: thrusters and roll in free space,
 * the spinning station, the docking check, crashes, debris, fuel, the
 * levels, and the autopilot (lines up on the axis, matches the slot's
 * roll and spin, then closes in slowly, holding back while debris
 * crosses). Fixed point throughout, so the host, the classic Amiga and
 * AmigaOS 4 fly a seed the same way.
 *
 * port: the original's od_core.c on fix16.h; port: marks where 16-bit int
 * needed widening (the same results: tools/check_core.sh).
 */
#include <string.h>
#include "od.h"


const char *const od_event_names[ODE_COUNT] = {
    "NONE", "DOCK", "CRASH", "DEBRIS", "FUEL_OUT", "LEVEL", "OVER", "WIN", "LOST"
};
const char *const od_fail_names[8] = {
    "", "OFF THE AXIS", "TOO FAST", "DRIFTING ACROSS", "ROLLED WRONG", "NOT TURNING WITH IT", "HIT THE HUB", "HIT DEBRIS"
};

/* by level: spin (degrees a second), start offset across, fuel, debris */
static const int16_t lv_spin[OD_LEVELS] = { 4, 7, 10, 10, 14, -18 };
static const int16_t lv_offset[OD_LEVELS] = { 0, 60, 140, 100, 160, 180 };
static const int16_t lv_fuel[OD_LEVELS] = { 600, 550, 500, 500, 550, 700 };
static const int8_t lv_debris[OD_LEVELS] = { 0, 0, 0, 1, 2, 3 };

static void event(ODGame *g, int type, int32_t a, int32_t b)
{
    if (g->nev < OD_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}

int od_pop_event(ODGame *g, ODEvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (g->nev - 1) * sizeof(ODEvent));
    g->nev--;
    return 1;
}

static uint32_t rnd(ODGame *g)
{
    uint32_t t = (g->rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

static fix fabs_(fix a) { return a < 0 ? -a : a; }
static int32_t iabs(int32_t a) { return a < 0 ? -a : a; }

int od_roll_error(const ODGame *g)
{
    int32_t d = ((g->roll - g->st_angle) >> 8) & 0x7FFF;     /* the slot repeats every half turn */
    return d >= 0x4000 ? (int)(d - 0x8000) : (int)d;
}

/* port: the debris' place across in fix16's multiply and divide (the same
   values: fdiv(m, period) = (m << 16) / period for m >= 0, fmul(amp << 16,
   sin) = amp * sin): zsdcc's 32-bit code for the original's expression
   returned 0 or its low 16 bits */
static fix debris_at(const Debris *d, int32_t ticks)
{
    fix m = ticks % d->period, q = fdiv(m, d->period), s;
    uint16_t a = (uint16_t)((q + d->phase) & 0xFFFF);
    s = a3_sin(a);
    return fmul(FROMI(d->amp), s);
}

fix od_debris_x(const ODGame *g, int k)
{
    return debris_at(&g->debris[k], g->level_ticks);
}

static void place(ODGame *g)
{
    int32_t a = (int32_t)(rnd(g) & 0xFFFF), off = lv_offset[g->level - 1];
    fix c = a3_cos(a), s = a3_sin(a);        /* port: the calls outside the products */
    g->x = FROMI((off * c) >> 16);
    g->y = FROMI((off * s) >> 16);
    g->z = FROMI(OD_START_Z);
    g->vx = g->vy = g->vz = 0;
    g->roll = (int32_t)(rnd(g) & 0xFFFF) << 8;
    g->roll_rate = 0;
}

void od_load_level(ODGame *g, int level)
{
    int k;
    g->level = level;
    g->st_angle = 0;
    g->st_spin = (int32_t)lv_spin[level - 1] * 65536 / 360 * 256 / 25;
    g->fuel = g->fuel_max = FROMI(lv_fuel[level - 1]);
    g->ndebris = lv_debris[level - 1];
    for (k = 0; k < g->ndebris; k++) {
        Debris *d = &g->debris[k];
        d->z = 520 - k * 140;
        d->y = (int32_t)(rnd(g) % 61) - 30;
        d->amp = 140 + (int32_t)(rnd(g) % 60);
        d->period = 150 + (int32_t)(rnd(g) % 100);
        d->phase = (int32_t)(rnd(g) & 0xFFFF);
    }
    g->level_ticks = 0;
    place(g);
}

void od_new_game(ODGame *g, uint32_t seed, int level)
{
    memset(g, 0, sizeof(*g));
    g->seed = g->rng = seed;
    g->lives = OD_LIVES;
    if (level < 1) level = 1;
    if (level > OD_LEVELS) level = OD_LEVELS;
    od_load_level(g, level);
}

static void crash(ODGame *g, int why)
{
    g->crashes++;
    g->lives--;
    g->last_fail = why;
    event(g, why == 7 ? ODE_DEBRIS : ODE_CRASH, why, g->lives);
    if (g->lives <= 0) { g->over = 1; event(g, ODE_OVER, g->score, 0); return; }
    event(g, ODE_LOST, g->lives, 0);
    g->fuel = g->fuel_max;
    place(g);
}

static void contact(ODGame *g)
{
    fix lat = fhypot(g->x, g->y);
    int why = 0;
    if (lat > FROMI(OD_HUB_R)) return;                          /* it misses the hub's face */
    if (lat > FROMI(OD_TOL_AXIS)) why = 1;
    else if (-g->vz > OD_TOL_CLOSE) why = 2;
    else if (fabs_(g->vx) > OD_TOL_DRIFT || fabs_(g->vy) > OD_TOL_DRIFT) why = 3;
    else if (iabs(od_roll_error(g)) > OD_TOL_ROLL) why = 4;
    else if (iabs(g->roll_rate - g->st_spin) > OD_TOL_SPIN) why = 5;
    if (why) { crash(g, why); return; }
    {
        int32_t bonus = 1000 + (int32_t)(g->fuel >> 16) * 2 + 500L * g->level - (g->level_ticks / 25) * 2;   /* port: 32 bits */
        if (bonus < 500) bonus = 500;
        g->score += bonus;
        g->docks++;
        g->between = OD_BETWEEN;
        g->z = FROMI(OD_PORT_Z + OD_NOSE);
        g->vx = g->vy = g->vz = 0;
        g->roll_rate = g->st_spin;
        event(g, ODE_DOCK, g->level, bonus);
    }
}

static void burn(ODGame *g, fix *v, fix dv, fix cost)
{
    if (g->fuel <= 0) return;
    *v += dv;
    g->fuel -= cost;
    if (g->fuel <= 0) { g->fuel = 0; event(g, ODE_FUEL_OUT, 0, 0); }
}

void od_tick(ODGame *g, unsigned in)
{
    int k;
    fix zprev;
    if (g->over || g->won) return;
    g->tick++;
    g->st_angle = (g->st_angle + g->st_spin) & 0xFFFFFF;    /* a8: a turn is 1 << 24 */
    if (g->between > 0) {
        g->roll = (g->roll + g->roll_rate) & 0xFFFFFF;          /* docked: turning with it */
        if (--g->between == 0) {
            if (g->level >= OD_LEVELS) { g->won = 1; event(g, ODE_WIN, g->score, 0); return; }
            od_load_level(g, g->level + 1);
            event(g, ODE_LEVEL, g->level, 0);
        }
        return;
    }
    g->level_ticks++;
    /* the thrusters across are the craft's own: right is the view's right,
     * whatever the roll (looking back down the axis, the view's right is
     * the station's (-cos, -sin) of the roll, its up (-sin, cos)) */
    {
        int bx = (in & OD_IN_RIGHT ? 1 : 0) - (in & OD_IN_LEFT ? 1 : 0), by = (in & OD_IN_UP ? 1 : 0) - (in & OD_IN_DOWN ? 1 : 0);
        if ((bx || by) && g->fuel > 0) {
            uint16_t a = (uint16_t)((g->roll >> 8) & 0xFFFF);
            fix c = a3_cos(a), sn = a3_sin(a);
            fix ax = FMUL(OD_ACC, -(fix)bx * c - (fix)by * sn), ay = FMUL(OD_ACC, -(fix)bx * sn + (fix)by * c);
            burn(g, &g->vx, ax, (bx ? FONE : 0) + (by ? FONE : 0));
            g->vy += ay;
        }
    }
    if (in & OD_IN_FWD) burn(g, &g->vz, -OD_ACC, FONE);
    if (in & OD_IN_BACK) burn(g, &g->vz, OD_ACC, FONE);
    if (g->fuel > 0 && (in & (OD_IN_ROLL_L | OD_IN_ROLL_R))) {
        g->roll_rate += (in & OD_IN_ROLL_R) ? OD_ROLL_ACC : -OD_ROLL_ACC;
        g->fuel -= FONE / 2;
        if (g->fuel <= 0) { g->fuel = 0; event(g, ODE_FUEL_OUT, 0, 0); }
    }
    g->roll = (g->roll + g->roll_rate) & 0xFFFFFF;
    zprev = g->z;
    g->x += g->vx;
    g->y += g->vy;
    g->z += g->vz;
    /* port: the tests below on locals, each compare on its own (zsdcc got
       the debris test's chain of 32-bit compares on fields wrong: a crash
       22.003 units from the debris' slice) */
    {
        fix x = g->x, y = g->y, z = g->z;
        /* drifted right away: lost in space */
        if (z > FROMI(1600) || fabs_(x) > FROMI(900) || fabs_(y) > FROMI(900)) { crash(g, 1); return; }
        /* debris */
        for (k = 0; k < g->ndebris; k++) {
            const Debris *d = &g->debris[k];
            fix dz = fabs_(z - FROMI(d->z)), dy, dx;
            if (dz >= FROMI(22)) continue;
            dy = fabs_(y - FROMI(d->y));
            if (dy >= FROMI(26)) continue;
            dx = fabs_(x - od_debris_x(g, k));
            if (dx < FROMI(26)) { crash(g, 7); return; }
        }
        /* the hub: its face, and its side */
        if (z <= FROMI(OD_PORT_Z + OD_NOSE) && zprev > FROMI(OD_PORT_Z + OD_NOSE)) contact(g);
        else if (z < FROMI(OD_PORT_Z + OD_NOSE) && z > -FROMI(300) && fhypot(x, y) < FROMI(OD_HUB_R + 10)) crash(g, 6);
    }
}

/* ---------- the autopilot ---------- */

/* ticks to cover d starting at speed v0 (both towards the station),
 * thrusting up to speed vc */
static int32_t ticks_to(fix d, fix v0, fix vc)
{
    fix t_acc, d_acc;
    if (d <= 0) return 0;
    if (v0 < 0) v0 = 0;
    if (v0 >= vc) return (int32_t)(d / vc);
    t_acc = FDIV(vc - v0, OD_ACC);
    d_acc = FMUL((v0 + vc) / 2, t_acc);
    if (d <= d_acc) return (int32_t)(FDIV(fsqrt(FMUL(v0, v0) + 2 * FMUL(OD_ACC, d)) - v0, OD_ACC) >> 16);
    return (int32_t)((t_acc >> 16) + (d - d_acc) / vc);
}

static fix clampf(fix v, fix lo, fix hi) { return v < lo ? lo : v > hi ? hi : v; }

/* port: the autopilot works on locals (the fields read once): zsdcc's code
   for 32-bit fields through the pointer, and for the long expressions
   on them, left a thrust out at one tick of the long autopilot game */
static unsigned bot_debris(const Debris *d, fix z, fix x, fix vz, int32_t level_ticks, fix *want)
{
    fix ahead = z - FROMI(d->z), sp;
    int32_t t0, t1, t;
    uint8_t clear = 1;
    if (ahead <= -FROMI(30) || ahead > FROMI(450)) return 0;
    if (ahead <= -FROMI(26)) return 0;
    if (*want > -FX(1.5)) *want = -FX(1.5);              /* through its slice briskly */
    sp = FX(1.5);
    t0 = ticks_to(ahead - FROMI(30), -vz, sp);
    t1 = ticks_to(ahead + FROMI(30), -vz, sp);
    if (t0 < 0) t0 = 0;
    for (t = t0 - 6; t <= t1 + 6 && clear; t += 3) {
        int32_t when = level_ticks + (t > 0 ? t : 0), dx;
        dx = debris_at(d, when) - x;
        if (dx < 0) dx = -dx;
        if (dx < FROMI(70)) clear = 0;
    }
    if (!clear) *want = ahead > FROMI(60) ? 0 : FX(0.8);
    return 0;
}

unsigned od_bot(const ODGame *g)
{
    unsigned in = 0;
    fix want, lat, x, y, z, vx, vy, vz;
    int32_t err, rate_want, st_spin, roll_rate, spin_err;
    int k, aligned;
    if (g->over || g->won || g->between) return 0;
    x = g->x; y = g->y; z = g->z; vx = g->vx; vy = g->vy; vz = g->vz;
    st_spin = g->st_spin; roll_rate = g->roll_rate;
    /* across: towards the axis, gently - the speed wanted, turned into the
     * craft's own right and up */
    {
        fix wx = clampf(-x / 30, -FX(1.0), FX(1.0)) - vx, wy = clampf(-y / 30, -FX(1.0), FX(1.0)) - vy, br, bu;
        uint16_t a = (uint16_t)((g->roll >> 8) & 0xFFFF);
        fix c = a3_cos(a), sn = a3_sin(a);
        br = FMUL(wx, -c) + FMUL(wy, -sn);
        bu = FMUL(wx, -sn) + FMUL(wy, c);
        if (br > FX(0.03)) in |= OD_IN_RIGHT;
        else if (br < -FX(0.03)) in |= OD_IN_LEFT;
        if (bu > FX(0.03)) in |= OD_IN_UP;
        else if (bu < -FX(0.03)) in |= OD_IN_DOWN;
    }
    /* roll: onto the slot, turning with it */
    err = od_roll_error(g);
    rate_want = st_spin - (err * 256 / 12);                       /* port: 32 bits */
    if (rate_want > st_spin + (140L << 8)) rate_want = st_spin + (140L << 8);
    if (rate_want < st_spin - (140L << 8)) rate_want = st_spin - (140L << 8);
    if (roll_rate < rate_want - (3L << 8)) in |= OD_IN_ROLL_R;
    else if (roll_rate > rate_want + (3L << 8)) in |= OD_IN_ROLL_L;
    /* along: in slowly once lined up; hold off otherwise */
    lat = fhypot(x, y);
    spin_err = iabs(roll_rate - st_spin);
    aligned = lat < FROMI(6) && iabs(err) < 700 && spin_err < (8L << 8) && fabs_(vx) < FX(0.3) && fabs_(vy) < FX(0.3);
    if (aligned) want = -clampf((z - FROMI(OD_PORT_Z + OD_NOSE)) / 60, FX(0.7), FX(3.0));
    else if (z > FROMI(300)) want = -FX(0.6);
    else want = 0;
    /* debris ahead: through its slice at 1.5 a tick, and only
     * when it will be clear of us the whole way across; otherwise wait
     * short of it, or back out if already close */
    for (k = 0; k < g->ndebris; k++) bot_debris(&g->debris[k], z, x, vz, g->level_ticks, &want);
    {
        fix hi = want + FX(0.04), lo = want - FX(0.04);
        if (vz > hi) in |= OD_IN_FWD;
        else if (vz < lo) in |= OD_IN_BACK;
    }
    return in;
}

uint32_t od_state_hash(const ODGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    int32_t v[18];                      /* port: filled in turn (zsdcc: no non-constant initialisers) */
    v[0] = g->score; v[1] = g->level; v[2] = g->lives; v[3] = g->x; v[4] = g->y; v[5] = g->z; v[6] = g->vx;
    v[7] = g->vy; v[8] = g->vz; v[9] = g->roll; v[10] = g->roll_rate; v[11] = g->st_angle; v[12] = g->fuel;
    v[13] = g->crashes; v[14] = g->docks; v[15] = g->tick; v[16] = g->over; v[17] = g->won;
    for (i = 0; i < 18; i++) { h ^= (uint32_t)v[i]; h *= 16777619UL; }
    return h;
}
