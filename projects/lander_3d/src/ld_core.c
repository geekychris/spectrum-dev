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
 * rotation's sines and cosines are worked out once for the four feet and
 * the bell (the original works them out for each); and contact() is not
 * called while the craft is too high above the ground for any foot to
 * reach it (the result is the same: no touchdown).
 */
#include <string.h>
#include "ld.h"
#include "lv_rec.h"

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
fix ld_height(const Terrain *T, fix x, fix z)
{
    int n = T->n, i, j;
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
    return a + ab + ca + m;
}

fix ld_slope_cos(const Terrain *T, fix x, fix z)
{
    fix e = T->cell / 2;
    fix dx = ld_height(T, x + e, z), dz;
    dx -= ld_height(T, x - e, z);
    dz = ld_height(T, x, z + e);
    dz -= ld_height(T, x, z - e);
    return FDIV(2 * e, fhypot3(dx, 2 * e, dz));
}

/* ------------------------------------------------------------ the craft */
static const fix FEET[4][3] = { { FX(2.3), FX(-2), FX(2.3) }, { FX(-2.3), FX(-2), FX(2.3) },
                                { FX(2.3), FX(-2), FX(-2.3) }, { FX(-2.3), FX(-2), FX(-2.3) } };
static const fix BELL[3] = { 0, FX(-1.5), 0 };

/* a body vector rotated by pitch (about x) after roll (about z); port: the
   sines and cosines set once (rot_set) for several vectors */
static fix r_cr, r_sr, r_cp, r_sp, r_pitch = 1, r_roll = 1;    /* (1: not an angle a step leaves) */
static void rot_set(fix pitch, fix roll)
{
    /* port: kept while the angle stays (it does once the rates are below
       1/100 of a unit: the step adds rate / 100, truncated) */
    if (roll != r_roll || r_roll == 1) { r_cr = fcos(roll); r_sr = fsin(roll); r_roll = roll; }
    if (pitch != r_pitch || r_pitch == 1) { r_cp = fcos(pitch); r_sp = fsin(pitch); r_pitch = pitch; }
}
static void rot(const fix v[3], fix out[3])
{
    fix x1 = FMUL(v[0], r_cr), y1 = FMUL(v[0], r_sr), z1 = v[2], t;
    x1 -= FMUL(v[1], r_sr);
    y1 += FMUL(v[1], r_cr);
    out[0] = x1;
    t = FMUL(y1, r_cp);
    out[1] = t - FMUL(z1, r_sp);
    t = FMUL(y1, r_sp);
    out[2] = t + FMUL(z1, r_cp);
}

void ld_up(const Craft *c, fix u[3])
{
    static const fix up[3] = { 0, FONE, 0 };
    rot_set(c->pitch, c->roll);
    rot(up, u);
}

fix ld_tilt(const Craft *c)
{
    fix u[3], hh;
    ld_up(c, u);
    hh = fhypot(u[0], u[2]);
    return fatan2(hh, u[1]);
}

void ld_feet(const Craft *c, fix out[5][3])
{
    uint8_t k, d;
    rot_set(c->pitch, c->roll);
    for (k = 0; k < 4; k++) {
        rot(FEET[k], out[k]);
        for (d = 0; d < 3; d++) out[k][d] += c->p[d];
    }
    rot(BELL, out[4]);
    for (d = 0; d < 3; d++) out[4][d] += c->p[d];
}

fix ld_agl(const Level *L)
{
    fix g = ld_height(&L->T, L->c.p[0], L->c.p[2]);
    return L->c.p[1] - FX(2) - g;
}

static void push_ev(Level *L, int e) { if (L->nev < 8) L->events[L->nev++] = e; }
int ld_pop_event(Level *L)
{
    int e;
    if (!L->nev) return LDE_NONE;
    e = L->events[0];
    memmove(&L->events[0], &L->events[1], (L->nev - 1) * sizeof(int));
    L->nev--;
    return e;
}

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

static void finish(Level *L, int outcome)
{
    L->outcome = outcome;
    if (outcome != OUT_CRASHED && outcome != OUT_LOST) {
        L->c.v[0] = L->c.v[1] = L->c.v[2] = 0;
        L->c.wp = L->c.wr = 0;
    }
    L->c.throttle = 0;
    push_ev(L, outcome == OUT_LANDED ? LDE_LANDED : outcome == OUT_OFFPAD ? LDE_OFFPAD :
               outcome == OUT_CRASHED ? LDE_CRASHED : LDE_LOST);
}

/* touchdown: any foot or the bell at or below the ground decides it */
static fix fs[5][3];                    /* port: static (zsdcc's stack frames) */
static int contact(Level *L)
{
    Craft *c = &L->c;
    const LdWorld *W = &ld_worlds[L->world];
    fix pen = 0, vs, hs, tilt, t;
    int k, touching = 0, pad = -1;
    LdResult *res = &L->result;
    ld_feet(c, fs);
    for (k = 0; k < 5; k++) {
        fix g = ld_height(&L->T, fs[k][0], fs[k][2]), fy = fs[k][1];
        if (fy <= g) { touching = 1; if (g - fy > pen) pen = g - fy; }
    }
    if (!touching) return 0;
    c->p[1] += pen;
    vs = -c->v[1];
    hs = fhypot(c->v[0], c->v[2]);
    tilt = ld_tilt(c);
    for (k = 0; k < 3 && pad < 0; k++) {
        const Pad *p = &L->T.pads[k];
        fix px = p->x, pz = p->z, pr = p->r;
        int f, in = fhypot(c->p[0] - px, c->p[2] - pz) <= pr - FX(1.2);
        for (f = 0; f < 4 && in; f++) if (fhypot(fs[f][0] - px, fs[f][2] - pz) > pr + FX(0.2)) in = 0;
        if (in) pad = k;
    }
    memset(res, 0, sizeof(*res));
    res->vs = vs; res->hs = hs; res->tilt = tilt;
    res->vs_ok = vs <= W->lim_vs;
    res->hs_ok = hs <= W->lim_hs;
    res->tilt_ok = tilt <= W->lim_tilt;
    res->pad = pad >= 0 ? L->T.pads[pad].mult : 0;
    res->slope = ld_slope_cos(&L->T, c->p[0], c->p[2]);
    if (res->vs_ok && res->hs_ok && res->tilt_ok && pad >= 0) {
        /* port: through locals (zsdcc and a multiply by a call's result) */
        fix soft, q;
        q = FONE - FDIV(vs, W->lim_vs);
        soft = 60 * q;
        q = FONE - FDIV(tilt, W->lim_tilt);
        soft += 30 * q;
        q = FONE - FDIV(hs, W->lim_hs);
        soft += 20 * q;
        res->soft = (soft + FONE / 2) >> 16;
        q = FDIV(L->fuel, L->fuel_max);
        t = FMUL(q, FROMI(150));
        res->fuel_bonus = (int32_t)((t + FONE / 2) >> 16);
        res->score = (100 + res->soft + res->fuel_bonus) * (int32_t)res->pad;
        finish(L, OUT_LANDED);
    } else if (res->vs_ok && res->hs_ok && res->tilt_ok && res->slope >= FX(0.97030)) {   /* <= 14 degrees */
        res->score = 25;
        finish(L, OUT_OFFPAD);
    } else {
        finish(L, OUT_CRASHED);
    }
    return 1;
}

/* port: the highest ground any foot or the bell could touch, the craft at
   (x, z): they are within 4 m of its middle (3.82 m for a foot), and
   ld_height() is never above the highest of its cell's four corners, so
   the corners of the cells it would read for x +- 4, z +- 4 bound it */
static fix ground_max_near(const Terrain *T, fix x, fix z)
{
    fix lim = FROMI(T->n - 1) - 1, f, best = T->hbase, h;
    uint8_t i0, i1, j0, j1, i, j;
    f = fclamp(FMUL(x - FROMI(4) + T->half, T->inv_cell), 0, lim); i0 = (uint8_t)FINT(f);
    f = fclamp(FMUL(x + FROMI(4) + T->half, T->inv_cell), 0, lim); i1 = (uint8_t)FINT(f) + 1;
    f = fclamp(FMUL(z - FROMI(4) + T->half, T->inv_cell), 0, lim); j0 = (uint8_t)FINT(f);
    f = fclamp(FMUL(z + FROMI(4) + T->half, T->inv_cell), 0, lim); j1 = (uint8_t)FINT(f) + 1;
    for (j = j0; j <= j1; j++)
        for (i = i0; i <= i1; i++) {
            h = ter_h(T, i, j);
            if (h > best) best = h;
        }
    return best;
}

#define RATE      FX(1.1)
#define ALPHA     FX(2.4)
#define RCS_FUEL  FX(0.06)
#define MAX_TILT  FX(1.30900)           /* 75 degrees */

/* port: ld_step() works on a copy of the craft in static memory (zsdcc's
   code through a pointer is several times slower) and divides by 100
   with div100() (ld.h; the same truncated quotient) */
static Craft C;

/* x / d truncated, d 2..64: shifts for 2, 4, 8 (the usual sub-steps) */
static fix divsub(fix x, int d)
{
    uint8_t k = d == 2 ? 1 : d == 4 ? 2 : d == 8 ? 3 : 0;
    if (!k) return x / d;
    return x < 0 ? -((-x) >> k) : x >> k;
}

static fix axis(const Level *L, fix w, fix cmd, uint8_t k)
{
    if (L->fuel <= 0) return w;
    if (L->assist) {
        fix err = FMUL(cmd, RATE) - w, a = fclamp(err * 6, -ALPHA, ALPHA);
        if (fabsx(a) > FX(0.2)) C.rcs[k] = a > 0 ? 1 : -1;
        return w + div100(a);
    }
    if (cmd) C.rcs[k] = cmd > 0 ? 1 : -1;
    return w + div100(FMUL(cmd, ALPHA));
}

void ld_step(Level *L, const LdInput *in)
{
    const LdWorld *W = &ld_worlds[L->world];
    fix thr, u[3], a0, a1, a2, speed, burn, amax = W->amax, t, lim;
    int32_t subs32;
    int subs, s;

    L->t++;
    if (L->outcome) return;
    memcpy(&C, &L->c, sizeof(C));
    thr = fclamp(in->throttle, 0, FONE);
    if (L->fuel <= 0) thr = 0;
    C.rcs[0] = C.rcs[1] = 0;
    C.wp = axis(L, C.wp, fclamp(in->pitch, -FONE, FONE), 0);
    C.wr = axis(L, C.wr, fclamp(in->roll, -FONE, FONE), 1);
    C.pitch = fclamp(C.pitch + div100(C.wp), -MAX_TILT, MAX_TILT);
    C.roll = fclamp(C.roll + div100(C.wr), -MAX_TILT, MAX_TILT);
    if (fabsx(C.pitch) >= MAX_TILT) C.wp = 0;
    if (fabsx(C.roll) >= MAX_TILT) C.wr = 0;
    burn = div100(thr) + div100((fix)((C.rcs[0] != 0) + (C.rcs[1] != 0)) * RCS_FUEL);
    if (L->fuel > 0) {
        L->fuel -= burn;
        if (L->fuel <= 0) { L->fuel = 0; push_ev(L, LDE_FUEL_OUT); }
    }
    C.throttle = thr;
    ld_up(&C, u);
    t = FMUL(u[0], amax); a0 = FMUL(t, thr);
    t = FMUL(u[1], amax); a1 = FMUL(t, thr) - W->g;
    t = FMUL(u[2], amax); a2 = FMUL(t, thr);
    wind_at(L, (fix)L->t * LD_DT);
    if (W->drag) {
        fix drag = W->drag;
        a0 += FMUL(drag, L->wind[0] - C.v[0]);
        a2 += FMUL(drag, L->wind[1] - C.v[2]);
        t = FMUL(drag, FX(0.3));
        a1 -= FMUL(t, C.v[1]);
    }
    /* sub-steps keep the touchdown check honest at any speed */
    speed = fhypot3(C.v[0], C.v[1], C.v[2]) + div100(amax);
    subs32 = (div100(speed) + FX(0.25) - 1) >> 14;      /* port: / FX(0.25) (>= 0); 32 bits before the clamp */
    subs = subs32 < 1 ? 1 : subs32 > 64 ? 64 : (int)subs32;
    for (s = 0; s < subs; s++) {
        if (subs == 1) {
            C.v[0] += div100(a0); C.v[1] += div100(a1); C.v[2] += div100(a2);
            C.p[0] += div100(C.v[0]); C.p[1] += div100(C.v[1]); C.p[2] += div100(C.v[2]);
        } else {
            /* x / (100 subs) = (x / 100) / subs, both truncated */
            C.v[0] += divsub(div100(a0), subs); C.v[1] += divsub(div100(a1), subs); C.v[2] += divsub(div100(a2), subs);
            C.p[0] += divsub(div100(C.v[0]), subs); C.p[1] += divsub(div100(C.v[1]), subs);
            C.p[2] += divsub(div100(C.v[2]), subs);
        }
        /* port: no foot can reach the ground from here (ground_max_near) */
        t = C.p[1] - FROMI(4);
        if (t > L->T.hmax) continue;
        if (t > ground_max_near(&L->T, C.p[0], C.p[2])) continue;
        memcpy(&L->c, &C, sizeof(C));
        if (contact(L)) return;
    }
    memcpy(&L->c, &C, sizeof(C));
    lim = FMUL(L->T.half, FX(1.15));
    if (fabsx(C.p[0]) > lim || fabsx(C.p[2]) > lim || C.p[1] > FROMI(6000))
        finish(L, OUT_LOST);
}

int ld_nearest_pad(const Level *L, int easiest)
{
    int k, pick = 0;
    fix best = 0x7FFFFFFFL;
    for (k = 0; k < 3; k++) {
        const Pad *p = &L->T.pads[k];
        fix d = fhypot(p->x - L->c.p[0], p->z - L->c.p[2]), s = easiest ? FDIV(d, p->r) : d;
        if (s < best) { best = s; pick = k; }
    }
    return pick;
}

/* ------------------------------------------------------------ levels */
/* port: the original makes the level here (ld_make_terrain, the start, the
   probe flight that sizes the tank); the Next reads what the original's
   code made on the host (tools/gen_levels.c) */
void ld_new_level(Level *L, int world, int level)
{
    uint8_t r, k;
    int lv = level < 1 ? 1 : level;
    if (lv > LV_PER_WORLD) lv = LV_PER_WORLD - 1 + (lv - LV_PER_WORLD + 1) % 2;   /* round the last two */
    r = (uint8_t)(world * LV_PER_WORLD + lv - 1);
    memset(L, 0, sizeof(*L));
    L->world = world;
    L->level = level;
    L->seed = (uint32_t)lv_field(r, LVF_SEED);
    for (k = 0; k < 6; k++) (&L->params.fuel)[k] = lv_field(r, LVF_PARAMS + k);
    L->T.world = world;
    L->T.n = (int)lv_field(r, LVF_N);
    L->T.size = (int)lv_field(r, LVF_SIZE);
    L->T.cell = lv_field(r, LVF_CELL);
    L->T.half = lv_field(r, LVF_HALF);
    L->T.inv_cell = lv_field(r, LVF_INV_CELL);
    for (k = 0; k < 3; k++) {
        Pad *p = &L->T.pads[k];
        p->x = lv_field(r, LVF_PADS + k * 5);
        p->z = lv_field(r, LVF_PADS + k * 5 + 1);
        p->r = lv_field(r, LVF_PADS + k * 5 + 2);
        p->y = lv_field(r, LVF_PADS + k * 5 + 3);
        p->mult = (int)lv_field(r, LVF_PADS + k * 5 + 4);
    }
    L->T.hmax = lv_field(r, LVF_HMAX);
    L->T.hbase = lv_field(r, LVF_HBASE);
    L->T.rpp = (uint8_t)lv_field(r, LVF_RPP);
    L->T.rowb = (uint16_t)lv_field(r, LVF_ROWB);
    L->T.page = (uint8_t)lv_field(r, LVF_PAGE);
    for (k = 0; k < 3; k++) {
        L->c.p[k] = lv_field(r, LVF_P + k);
        L->c.v[k] = lv_field(r, LVF_V + k);
    }
    L->c.pitch = lv_field(r, LVF_PITCH);
    L->c.roll = lv_field(r, LVF_ROLL);
    L->c.wp = lv_field(r, LVF_WP);
    L->c.wr = lv_field(r, LVF_WR);
    L->c.throttle = lv_field(r, LVF_THROTTLE);
    L->fuel = lv_field(r, LVF_FUEL);
    L->fuel_max = lv_field(r, LVF_FUEL_MAX);
    L->wind_dir = lv_field(r, LVF_WIND_DIR);
    L->wind_ph = lv_field(r, LVF_WIND_PH);
    L->probe_used = lv_field(r, LVF_PROBE_USED);
    L->probe_outcome = (int)lv_field(r, LVF_PROBE_OUTCOME);
    L->assist = 1;
    L->ap_target = -1;
}
