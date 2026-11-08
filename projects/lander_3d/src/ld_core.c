// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ld_core.c - Lander 3D's simulation (see ld.h), from the browser's
 * lander-3d-core.js, in 16.16 fixed point.
 *
 * port: the original's (tools/orig/ld_core.c) on fix16.h. The terrain, the
 * pads and the level's start are made on the host by the original's code
 * (tools/gen_levels.c); here a level is read from its record. Changes for
 * zsdcc and speed keep the values (tools/check_core.sh): `int` is 16 bits,
 * so a few sums are 32-bit; zsdcc's code for a multiply with a call as an
 * operand is unreliable, so such results go through locals; the
 * rotation's sines and cosines, the up vector and the feet's turned offsets
 * are kept while the angles stay (the original works them out each time);
 * contact() is not called while the craft is too high above the ground
 * for any foot to reach it, and in it a foot above its cell's highest
 * corner isn't looked up (it can't touch: the result is the same).
 * The touchdown, the events and a level's start are in ld_land.c.
 */
#include <string.h>
#include "ld.h"
#include "lv_rec.h"
#include "prof.h"
#include "ld_int.h"

/* ------------------------------------------------------------ maths */
static fix fclamp(fix v, fix lo, fix hi) { return v < lo ? lo : v > hi ? hi : v; }
static fix fabsx(fix v) { return v < 0 ? -v : v; }

/* ------------------------------------------------------------ worlds */
const LdWorld ld_worlds[W_COUNT] = {
    /* name        g          amax      size  n   hills     rough      craters max wind     drag       curve       dunes  vs        hs        tilt (rad)    fuel */
    { "MOON",     FX(1.62), FX(4.4), 1200, 97, FX(38), FX(0.5),  34, 90, 0,       0,         0,          0,     FX(2.5), FX(1.5), FX(0.17453), FX(62) },
    { "MARS",     FX(3.71), FX(9.0), 1200, 97, FX(55), FX(0.45), 10, 110, FX(3),  FX(0.04),  0,          FX(9), FX(3.0), FX(1.8), FX(0.17453), FX(66) },
    { "ASTEROID", FX(0.35), FX(1.5), 640,  65, FX(26), FX(0.62), 26, 55, 0,       0,         FX(900),    0,     FX(1.5), FX(1.0), FX(0.20944), FX(85) },
};
const char *const ld_outcome_names[5] = { "flying", "landed", "offpad", "crashed", "lost" };

/* ------------------------------------------------------------ terrain */
/* port: the last answer kept (the craft's spot is asked for again by the
   camera, the HUD, the shadow) */
static const Terrain *lh_t;
static fix lh_x, lh_z, lh_h;
fix ld_height(const Terrain *T, fix x, fix z)
{
    int n = T->n, i, j;
    if (T == lh_t && x == lh_x && z == lh_z) return lh_h;
    fix fx = fclamp(FMUL(x + T->half, T->inv_cell), 0, FROMI(n - 1) - 1);
    fix fz = fclamp(FMUL(z + T->half, T->inv_cell), 0, FROMI(n - 1) - 1);
    fix tx, tz, a, b, c, d, h[4], ab, ca, abcd, m;
    i = (int)FINT(fx); j = (int)FINT(fz);
    tx = fx & 0xFFFF; tz = fz & 0xFFFF;
    ter_cell(T, (uint8_t)i, (uint8_t)j, h);             /* port: the heights from their page */
    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    ab = FMUL(b - a, tx);
    ca = FMUL(c - a, tz);
    abcd = FMUL(a - b - c + d, tx);
    m = FMUL(abcd, tz);
    lh_t = T; lh_x = x; lh_z = z;
    return lh_h = a + ab + ca + m;
}

/* ------------------------------------------------------------ the craft */
/* a body vector rotated by pitch (about x) after roll (about z); port: the
   sines and cosines set once (rot_set) for several vectors */
fix r_cr, r_sr, r_cp, r_sp;
static fix r_pitch = 1, r_roll = 1;    /* (1: not an angle a step leaves) */
void ld_rot_set(fix pitch, fix roll)
{
    /* port: kept while the angle stays (it does once the rates are below
       1/100 of a unit: the step adds rate / 100, truncated) */
    /* port: fsin and fcos of an angle from one a3_angle() (fix16.c: the
       same values) */
    uint16_t a;
    if (roll != r_roll || r_roll == 1) { a = (uint16_t)a3_angle(roll); r_cr = a3_sin(a + 16384); r_sr = a3_sin(a); r_roll = roll; }
    if (pitch != r_pitch || r_pitch == 1) { a = (uint16_t)a3_angle(pitch); r_cp = a3_sin(a + 16384); r_sp = a3_sin(a); r_pitch = pitch; }
}
/* port: the last result kept for the same angles (ld_step's every step) */
static fix up_cache[3], up_pitch = 1, up_roll = 1;
void ld_up(const Craft *c, fix u[3])
{
    if (c->pitch != up_pitch || c->roll != up_roll || up_pitch == 1) {
        ld_rot_set(c->pitch, c->roll);
        /* port: rot(up): with v = (0, 1, 0) its products by 0 are 0 and by
           1 the other operand (fmul is floor(a b / 65536), exact there) */
        up_cache[0] = -r_sr;
        up_cache[1] = FMUL(r_cr, r_cp);
        up_cache[2] = FMUL(r_cr, r_sp);
        up_pitch = c->pitch;
        up_roll = c->roll;
    }
    u[0] = up_cache[0]; u[1] = up_cache[1]; u[2] = up_cache[2];
}

void ld_push_ev(Level *L, int e) { if (L->nev < 8) L->events[L->nev++] = e; }
static void wind_at(Level *L, fix t)
{
    fix s, a, s1, s2, s3, ph = L->wind_ph, w = L->params.wind;
    if (!w) { L->wind[0] = L->wind[1] = 0; return; }
    s1 = fsin(FMUL(t, FX(0.21)) + ph);
    s2 = fsin(FMUL(t, FX(1.3)) + 2 * ph);
    s3 = fsin(FMUL(t, FX(0.37)));
    s = FX(0.65) + FMUL(FX(0.25), s1);
    s2 = FMUL(FX(0.18), s2);
    s += FMUL(s2, s3);
    s = FMUL(w, s);
    s1 = fsin(FMUL(t, FX(0.13)) + ph);
    a = L->wind_dir + FMUL(FX(0.35), s1);
    s1 = fcos(a);
    L->wind[0] = FMUL(s1, s);
    s1 = fsin(a);
    L->wind[1] = FMUL(s1, s);
}

/* port: the cell ld_height() reads at (x, z) (the same clamp) */
void ld_cell_of(const Terrain *T, fix x, fix z, uint8_t *i, uint8_t *j)
{
    fix lim = FROMI(T->n - 1) - 1;
    *i = (uint8_t)FINT(fclamp(FMUL(x + T->half, T->inv_cell), 0, lim));
    *j = (uint8_t)FINT(fclamp(FMUL(z + T->half, T->inv_cell), 0, lim));
}

/* port: the highest ground any foot or the bell could touch, the craft at
   (x, z): they are within 4 m of its middle (3.82 m for a foot), and
   ld_height() is never above the highest of its cell's four corners, so
   the corners of the cells it would read for x +- 4, z +- 4 bound it. It is
   worked out for x +- 8, z +- 8 and kept while the craft is within 4 m of
   where it was (cell_of() only grows with x and z: still a bound) */
static fix gm_x, gm_z, gm_best;
static uint8_t gm_ok;
static fix ground_max_near(const Terrain *T, fix x, fix z)
{
    fix best = T->hbase, h, d;
    uint8_t i0, i1, j0, j1, i, j;
    if (gm_ok) {
        d = x - gm_x;
        if (d <= FROMI(4) && d >= -FROMI(4)) {
            d = z - gm_z;
            if (d <= FROMI(4) && d >= -FROMI(4)) return gm_best;
        }
    }
    ld_cell_of(T, x - FROMI(8), z - FROMI(8), &i0, &j0);
    ld_cell_of(T, x + FROMI(8), z + FROMI(8), &i1, &j1);
    for (j = j0; j <= j1 + 1; j++)
        for (i = i0; i <= i1 + 1; i++) {
            h = ter_h(T, i, j);
            if (h > best) best = h;
        }
    gm_x = x;
    gm_z = z;
    gm_best = best;
    gm_ok = 1;
    return best;
}

/* port: the original's bound itself (x +- 4, z +- 4), for when the kept
   one doesn't settle it (low flight); kept while the cells are the same */
static uint8_t g4_i0 = 0xFF, g4_j0, g4_i1, g4_j1;
static fix g4_best;
static fix ground_max_4(const Terrain *T, fix x, fix z)
{
    fix best = T->hbase, h;
    uint8_t i0, i1, j0, j1, i, j;
    ld_cell_of(T, x - FROMI(4), z - FROMI(4), &i0, &j0);
    ld_cell_of(T, x + FROMI(4), z + FROMI(4), &i1, &j1);
    if (i0 == g4_i0 && j0 == g4_j0 && i1 == g4_i1 && j1 == g4_j1) return g4_best;
    for (j = j0; j <= j1 + 1; j++)
        for (i = i0; i <= i1 + 1; i++) {
            h = ter_h(T, i, j);
            if (h > best) best = h;
        }
    g4_i0 = i0; g4_j0 = j0; g4_i1 = i1; g4_j1 = j1;
    return g4_best = best;
}

/* port: the lowest any foot (or the bell, always above them) can be below
   the craft's middle: the feet (+-2.3, -2, +-2.3) turned as ld_feet() does,
   y = (x sin r + y cos r) cos p - z sin p, at its lowest (cos p, cos r > 0
   within 75 degrees), less 0.01 m for the roundings of contact()'s own
   products (a few 1/65536 m). From ld_rot_set()'s sines */
static fix foot_low(void)
{
    fix a = FMUL(FX(2.3), r_sr), b = FMUL(FX(2), r_cr), c = FMUL(FX(2.3), r_sp), y1;
    if (a < 0) a = -a;
    if (c < 0) c = -c;
    y1 = -a - b;
    return FMUL(y1, r_cp) - c - FX(0.01);
}

/* port: a new ground: the kept answers go (ld_new_level) */
void ld_caches_reset(void) { gm_ok = 0; lh_t = 0; g4_i0 = 0xFF; }

#define RATE      FX(1.1)
#define ALPHA     FX(2.4)
#define RCS_FUEL  FX(0.06)
#define MAX_TILT  FX(1.30900)           /* 75 degrees */

/* port: ld_step() works on a copy of the craft in static memory (zsdcc's
   code through a pointer is several times slower) and divides by 100
   with div100() (ld.h; the same truncated quotient); on the Z80 its
   attitude and integration are ld_step.asm (the C below on the host) */
Craft ld_sc;
#define C ld_sc

#ifdef __SDCC
void ls_axes(const LdInput *in);
void ls_angles(void);
void ls_integrate(const fix *a);
#else
/* ld_step.asm's steps: the original's axis() with the assist on and fuel
   left (the clamps as compares, err * 6 as sums: the same values), for
   both axes; the angles; the integration of a sub-step of 1/100 s */
static fix ls_axis(fix w, fix cmd, uint8_t k)
{
    fix a, t;
    if (cmd < -FONE) cmd = -FONE; else if (cmd > FONE) cmd = FONE;
    a = FMUL(cmd, RATE) - w;
    t = a + a;
    a = t + t + t;
    if (a < -ALPHA) a = -ALPHA;
    else if (a > ALPHA) a = ALPHA;
    if (a > FX(0.2)) C.rcs[k] = 1;
    else if (a < -FX(0.2)) C.rcs[k] = -1;
    return w + div100(a);
}
static void ls_axes(const LdInput *in) { C.wp = ls_axis(C.wp, in->pitch, 0); C.wr = ls_axis(C.wr, in->roll, 1); }
static fix ls_angle(fix t, fix *rate)
{
    t += div100(*rate);
    if (t <= -MAX_TILT) { t = -MAX_TILT; *rate = 0; } else if (t >= MAX_TILT) { t = MAX_TILT; *rate = 0; }
    return t;
}
static void ls_angles(void) { C.pitch = ls_angle(C.pitch, &C.wp); C.roll = ls_angle(C.roll, &C.wr); }
static void ls_integrate(const fix *a)
{
    uint8_t k;
    for (k = 0; k < 3; k++) C.v[k] += div100(a[k]);
    for (k = 0; k < 3; k++) C.p[k] += div100(C.v[k]);
}
#endif

static const fix rcs_burn[3] = { 0, RCS_FUEL / 100, 2 * RCS_FUEL / 100 };     /* div100(n RCS_FUEL) */
static const fix amax100[W_COUNT] = { FX(4.4) / 100, FX(9.0) / 100, FX(1.5) / 100 };   /* div100(amax) */
static fix burn_thr = -1, burn_thr100, lost_half = -1, lost_lim;

void ld_step(Level *L, const LdInput *in)
{
    const LdWorld *W = &ld_worlds[L->world];
    static fix acc[3];                   /* (ls_integrate's) */
    fix thr, u[3], speed, burn, amax = W->amax, t, lim, low = 0;
    int32_t subs32;
    int subs, s;

    L->t++;
    if (L->outcome) return;
    memcpy(&C, &L->c, sizeof(C));
    /* port: the clamps as compares (the same values); a clamped angle is
       at the limit exactly when the original's |angle| >= MAX_TILT */
    thr = in->throttle;
    if (thr < 0 || L->fuel <= 0) thr = 0;
    else if (thr > FONE) thr = FONE;
    C.rcs[0] = C.rcs[1] = 0;
    /* the original's axis(): with no fuel the rates stay. port: the assist
       is always on (ld_new_level(); the original's switch for it isn't
       ported), so its other branch is left out */
    if (L->fuel > 0) ls_axes(in);
    ls_angles();
    /* port: div100 of the throttle kept while it stays; the thrusters'
       share is one of three constants */
    if (thr != burn_thr) { burn_thr = thr; burn_thr100 = div100(thr); }
    burn = burn_thr100 + rcs_burn[(C.rcs[0] != 0) + (C.rcs[1] != 0)];
    if (L->fuel > 0) {
        L->fuel -= burn;
        if (L->fuel <= 0) { L->fuel = 0; ld_push_ev(L, LDE_FUEL_OUT); }
    }
    C.throttle = thr;
    if (thr) {                           /* port: fmul(t, 0) is 0 */
        ld_up(&C, u);
        t = FMUL(u[0], amax); acc[0] = FMUL(t, thr);
        t = FMUL(u[1], amax); acc[1] = FMUL(t, thr) - W->g;
        t = FMUL(u[2], amax); acc[2] = FMUL(t, thr);
    } else { acc[0] = 0; acc[1] = -W->g; acc[2] = 0; }
    if (L->params.wind) wind_at(L, (fix)L->t * LD_DT);     /* port: no wind: L->wind stays 0 (wind_at's) */
    if (W->drag) {
        fix drag = W->drag;
        acc[0] += FMUL(drag, L->wind[0] - C.v[0]);
        acc[2] += FMUL(drag, L->wind[1] - C.v[2]);
        t = FMUL(drag, FX(0.3));
        acc[1] -= FMUL(t, C.v[1]);
    }
    /* sub-steps keep the touchdown check honest at any speed */
    /* port: fhypot3 is at most |x| + |y| + |z|: below about 24 m/s that
       already gives one sub-step, and the length isn't needed */
    {
        fix v0 = C.v[0], v1 = C.v[1], v2 = C.v[2];
        if (v0 < 0) v0 = -v0;
        if (v1 < 0) v1 = -v1;
        if (v2 < 0) v2 = -v2;
        t = v0 + v1 + v2 + amax100[L->world];
    }
    if (t >= 0 && t < FROMI(24)) subs = 1;
    else {
        speed = fhypot3(C.v[0], C.v[1], C.v[2]) + amax100[L->world];
        subs32 = (div100(speed) + FX(0.25) - 1) >> 14;  /* port: / FX(0.25) (>= 0); 32 bits before the clamp */
        subs = subs32 < 1 ? 1 : subs32 > 64 ? 64 : (int)subs32;
    }
    for (s = 0; s < subs; s++) {
        if (subs == 1) ls_integrate(acc);
        else {
            int16_t div = LD_STEP_HZ * subs;    /* (fast: above ~24 m/s only) */
            C.v[0] += acc[0] / div; C.v[1] += acc[1] / div; C.v[2] += acc[2] / div;
            C.p[0] += C.v[0] / div; C.p[1] += C.v[1] / div; C.p[2] += C.v[2] / div;
        }
        /* port: no foot can reach the ground from here (its lowest point
           against ground_max_near, then the original's x +- 4 bound;
           contact() then tests each foot against its cell's highest corner) */
        t = C.p[1] - FROMI(4);
        if (t > L->T.hmax) continue;
        if (!low) { ld_rot_set(C.pitch, C.roll); low = foot_low(); }   /* (the angles stay over the sub-steps) */
        t = C.p[1] + low;
        if (t > ground_max_near(&L->T, C.p[0], C.p[2])) continue;
        if (t > ground_max_4(&L->T, C.p[0], C.p[2])) continue;
        memcpy(&L->c, &C, sizeof(C));
        if (ld_contact(L)) return;
    }
    memcpy(&L->c, &C, sizeof(C));
    if (L->T.half != lost_half) { lost_half = L->T.half; lost_lim = FMUL(lost_half, FX(1.15)); }   /* port: kept */
    lim = lost_lim;
    if (fabsx(C.p[0]) > lim || fabsx(C.p[2]) > lim || C.p[1] > FROMI(6000))
        ld_finish(L, OUT_LOST);
}
