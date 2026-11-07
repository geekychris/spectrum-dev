/*
 * td_ai.c - Tank Duel's enemies' minds and the autopilot, from the
 * original's td_core.c (port: a file of their own, to fit the Next's 16K
 * banks; unchanged otherwise but for the "port:" notes).
 */
#include <stddef.h>
#include "td.h"
#include "td_int.h"

/* ---------- the enemies ---------- */
void think(TDGame *g, int k)
{
    Tank *t = &g->tk[k], *p = &g->tk[0];
    fix dx = p->x - t->x, dz = p->z - t->z, d = dist(dx, dz), speed = vmax[t->type];
    int to = td_angle_to(dx, dz), want = to;
    t->ai_timer++;
    if (t->reload > 0) t->reload--;
    switch (t->type) {
    case TK_SCOUT:
        if (t->ai_timer % 120 > 70) {           /* turn in to snipe */
            want = to;
            speed = FONE;
            if (iabs(adiff(to, t->yaw)) < 700 && d < FROMI(900)) td_fire(g, k);
        } else if (d > FROMI(420)) want = to + t->orbit * 0x1000;
        else if (d < FROMI(260)) want = to + t->orbit * 0x6000;
        else want = to + t->orbit * 0x4000;
        if (t->ai_timer % 400 == 399) t->orbit = -t->orbit;
        break;
    case TK_MORTAR:
        if (d < FROMI(450)) want = to + 0x8000;
        else if (d > FROMI(750)) want = to;
        else speed = 0;
        if (t->reload == 0 && d < FROMI(1100)) lob(g, k);
        break;
    case TK_BULWARK:
        if (d < FROMI(240)) speed = 0;
        if (iabs(adiff(to, t->yaw)) < 600 && d < FROMI(900)) td_fire(g, k);
        break;
    }
    /* something in the way ahead: steer round it */
    if (speed > 0 && td_blocked(g, t->x + 70 * a3_sin(t->yaw), t->z + 70 * a3_cos(t->yaw), TD_TANK_R))
        want = t->yaw + t->orbit * 0x2000;
    drive(g, t, want & 0xFFFF, speed);
}

/* ---------- the autopilot ---------- */
unsigned td_bot(const TDGame *g)
{
    const Tank *p = &g->tk[0], *e = NULL;
    fix best = 0, dx, dz, d;
    int k, want, diff, to;
    unsigned in = 0;
    if (g->over || g->won || g->between || !p->alive) return 0;
    for (k = 1; k < g->ntanks; k++) {
        const Tank *t = &g->tk[k];
        fix dd;
        if (!t->alive) continue;
        dd = dist(t->x - p->x, t->z - p->z);
        if (!e || dd < best) { e = t; best = dd; }
    }
    if (!e) return 0;
    dx = e->x - p->x;
    dz = e->z - p->z;
    d = best;
    to = td_angle_to(dx, dz);
    /* a bulwark: circle it at 320, the way it is already turning away
     * from, until well off its shield; then stop, aim and fire */
    if (e->type == TK_BULWARK) {
        int rel = adiff(td_angle_to(-dx, -dz), e->yaw), side = rel >= 0 ? 1 : -1;
        if (iabs(rel) < SHIELD_ARC + 2600) {
            int32_t lean32 = ((d - FROMI(320)) >> 16) * 40;    /* port: 32 bits */
            int lean;
            if (lean32 > 0x2000) lean32 = 0x2000;
            if (lean32 < -0x2000) lean32 = -0x2000;
            lean = (int)lean32;
            want = to - side * (0x4000 - lean);
            in |= TD_IN_UP;
        } else want = to;
    } else {
        want = to;
        if (d > FROMI(420)) in |= TD_IN_UP;
        else if (d < FROMI(230)) in |= TD_IN_DOWN;
    }
    /* out of a mortar's ring */
    for (k = 0; k < TD_MAX_MORTARS; k++) {
        const Mortar *m = &g->mo[k];
        if (m->alive && near(p->x - m->tx, p->z - m->tz, FROMI(TD_SPLASH_R + 40))) {
            want = td_angle_to(p->x - m->tx, p->z - m->tz);
            in = (in & ~TD_IN_DOWN) | TD_IN_UP;
        }
    }
    /* something in the way */
    if ((in & TD_IN_UP) && td_blocked(g, p->x + 60 * a3_sin(p->yaw), p->z + 60 * a3_cos(p->yaw), TD_TANK_R))
        want = p->yaw + 0x3000;
    diff = adiff(want, p->yaw);
    if (diff > 250) in |= TD_IN_RIGHT;
    if (diff < -250) in |= TD_IN_LEFT;
    /* shoot when lined up on an enemy and not at its shield */
    for (k = 1; k < g->ntanks; k++) {
        const Tank *t = &g->tk[k];
        fix tdx, tdz;
        if (!t->alive) continue;
        tdx = t->x - p->x;
        tdz = t->z - p->z;
        if (iabs(adiff(td_angle_to(tdx, tdz), p->yaw)) > 450 || beyond(tdx, tdz, FROMI(1000))) continue;
        if (t->type == TK_BULWARK && iabs(adiff(td_angle_to(-tdx, -tdz), t->yaw)) < SHIELD_ARC) continue;
        if (p->reload == 0) in |= TD_IN_FIRE;
    }
    return in;
}

