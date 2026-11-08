// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ld_auto.c - Lander 3D's autopilot (ld_core.c in the original): it flies
 * the title's demo and, in the original, the probe flight that sizes the
 * tank. port: its own file (zsdcc's code for the rules is over 16K: this
 * is linked into page 46 at 0x6000, ld_core.c into bank 20 at 0x0000).
 */
#include "ld.h"

static fix fclamp(fix v, fix lo, fix hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------ the autopilot */
void ld_autopilot(Level *L, LdInput *out)
{
    Craft *c = &L->c;
    const LdWorld *W = &ld_worlds[L->world];
    const Terrain *T = &L->T;
    const Pad *pad;
    fix dx, dz, dist, h, hi = -0x7FFFFFFFL, clearance, vmax, aH, vh, vdes[2], kh, ax, ay, az, vy_des, amag, ux, uy, uz, maxT;
    fix roll_t, pitch_t, q, cx = c->p[0], cy = c->p[1], cz = c->p[2], amax = W->amax, lim_hs = W->lim_hs;
    fix lim_vs = W->lim_vs, pr;
    int k;

    if (L->ap_target < 0) L->ap_target = ld_nearest_pad(L, 1);
    pad = &T->pads[L->ap_target];
    pr = pad->r;
    dx = pad->x - cx;
    dz = pad->z - cz;
    dist = fhypot(dx, dz);
    h = cy - FX(2) - pad->y;
    for (k = 0; k <= 8; k++) {
        fix lim = dist > FROMI(140) ? FDIV(FROMI(140), dist) : FONE;
        fix t = FMUL(FROMI(k) / 8, lim), g, gx, gz;
        gx = FMUL(dx, t);
        gz = FMUL(dz, t);
        g = ld_height(T, cx + gx, cz + gz);
        if (g > hi) hi = g;
    }
    clearance = cy - FX(2) - hi;
    vmax = FMUL(amax, FX(3.2));
    q = FROMI(4) + FMUL(dist, FX(0.14));
    if (q < vmax) vmax = q;
    aH = FMUL(amax, FX(0.40674));                 /* sin 24 degrees */
    vh = vmax;
    q = FMUL(dist, FX(0.28));
    if (q < vh) vh = q;
    q = FMUL(FX(0.6), aH);
    q = fsqrt(FMUL(q, dist));
    if (q < vh) vh = q;
    if (dist > FX(0.01)) {
        q = FDIV(dx, dist); vdes[0] = FMUL(q, vh);
        q = FDIV(dz, dist); vdes[1] = FMUL(q, vh);
    } else vdes[0] = vdes[1] = 0;
    if (L->ap_down) {
        fix cap = FMUL(lim_hs, FX(0.35)), m = fhypot(vdes[0], vdes[1]);
        if (m > cap) {
            q = FDIV(cap, m); vdes[0] = FMUL(vdes[0], q);
            q = FDIV(cap, m); vdes[1] = FMUL(vdes[1], q);
        }
    }
    kh = L->ap_down ? FX(1.4) : FX(0.9);
    ax = FMUL(vdes[0] - c->v[0], kh);
    az = FMUL(vdes[1] - c->v[2], kh);
    /* hold a cruise height while far; descend once over the pad (with
     * hysteresis, so gusts don't send it back up) */
    {
        fix six = FROMI(6), half_r = FMUL(pr, FX(0.45));
        if (dist < (six < half_r ? six : half_r)) L->ap_down = 1;
        else if (dist > FMUL(pr, FX(0.85))) L->ap_down = 0;
    }
    if (!L->ap_down) {
        fix cruise;
        if (dist < FROMI(60)) cruise = FROMI(14);
        else {
            q = FMUL(dist, FX(0.08));
            cruise = q > FROMI(25) ? q : FROMI(25);
        }
        vy_des = fclamp(FMUL(cruise - clearance, FX(0.35)), FROMI(-4), FROMI(6));
    } else {
        fix hi_lim = FMUL(lim_vs, FX(0.55)) + FMUL(h, FX(0.05));
        vy_des = -fclamp(FMUL(h, FX(0.12)) + FX(0.6), FX(0.6), hi_lim);
        if (h < FROMI(6)) {
            fix v1 = FMUL(lim_vs, FX(0.45)), v2 = FX(0.4) + FMUL(h, FX(0.15));
            vy_des = -(v1 < v2 ? v1 : v2);
        }
    }
    if (L->ap_down && h < FROMI(8) && vy_des < 0) {
        q = fhypot(c->v[0], c->v[2]);
        if (q > FMUL(lim_hs, FX(0.5))) vy_des = 0;
    }
    if (L->ap_down && h < FROMI(10) && vy_des < 0) {
        q = FMUL(pr, FX(0.5));
        if (dist > q) vy_des = 0;
    }
    ay = FMUL(vy_des - c->v[1], FX(1.6)) + W->g;
    amag = fhypot3(ax, ay, az);
    if (amag < 1) amag = 1;
    ux = FDIV(ax, amag); uy = FDIV(ay, amag); uz = FDIV(az, amag);
    if (h < FX(1.5)) maxT = FX(0.05236);
    else if (h < FROMI(6)) {
        q = W->lim_tilt - FX(0.01745);
        maxT = FX(0.15708) < q ? FX(0.15708) : q;
    } else maxT = FX(0.48869);                      /* 3, 9 (or the limit - 1), 28 degrees */
    q = fcos(maxT);
    if (uy < q) {
        fix hh = fhypot(ux, uz), s = fsin(maxT);
        if (hh < 1) hh = 1;
        q = FDIV(ux, hh); ux = FMUL(q, s);
        q = FDIV(uz, hh); uz = FMUL(q, s);
        uy = fcos(maxT);
    }
    {
        fix den = FMUL(amax, uy);
        out->throttle = den > 0 ? fclamp(FDIV(ay > 0 ? ay : 0, den), 0, FONE) : FONE;
    }
    /* the attitude that points the thrust that way: u = [-sin r, cos r cos p, cos r sin p] */
    roll_t = fasin(fclamp(-ux, -FONE, FONE));
    pitch_t = fatan2(uz, uy);
    q = FMUL(pitch_t - c->pitch, FX(2.8));
    out->pitch = fclamp(q - FMUL(c->wp, FX(0.4)), -FONE, FONE);
    q = FMUL(roll_t - c->roll, FX(2.8));
    out->roll = fclamp(q - FMUL(c->wr, FX(0.4)), -FONE, FONE);
}

