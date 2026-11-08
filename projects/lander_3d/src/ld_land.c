// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ld_land.c - the rest of Lander 3D's simulation (ld_core.c has the step):
 * the touchdown (the feet and the bell against the ground, the pads, the
 * score), the end of a flight, the events, and a level's start from its
 * record. Bank 20 with ld_core.c, compiled for size: it runs near the
 * ground only, and once a level.
 */
#include <string.h>
#include "ld.h"
#include "lv_rec.h"
#include "ld_int.h"

static const fix FEET[4][3] = { { FX(2.3), FX(-2), FX(2.3) }, { FX(-2.3), FX(-2), FX(2.3) },
                                { FX(2.3), FX(-2), FX(-2.3) }, { FX(-2.3), FX(-2), FX(-2.3) } };
static const fix BELL[3] = { 0, FX(-1.5), 0 };

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

/* port: the turned offsets kept while the angles stay */
static fix foot_off[5][3], foot_pitch = 1, foot_roll = 1;
void ld_feet(const Craft *c, fix out[5][3])
{
    uint8_t k, d;
    if (c->pitch != foot_pitch || c->roll != foot_roll || foot_pitch == 1) {
        ld_rot_set(c->pitch, c->roll);
        for (k = 0; k < 4; k++) rot(FEET[k], foot_off[k]);
        rot(BELL, foot_off[4]);
        foot_pitch = c->pitch;
        foot_roll = c->roll;
    }
    for (k = 0; k < 5; k++)
        for (d = 0; d < 3; d++) out[k][d] = foot_off[k][d] + c->p[d];
}

fix ld_agl(const Level *L)
{
    fix g = ld_height(&L->T, L->c.p[0], L->c.p[2]);
    return L->c.p[1] - FX(2) - g;
}

int ld_pop_event(Level *L)
{
    int e;
    if (!L->nev) return LDE_NONE;
    e = L->events[0];
    memmove(&L->events[0], &L->events[1], (L->nev - 1) * sizeof(int));
    L->nev--;
    return e;
}

void ld_finish(Level *L, int outcome)
{
    L->outcome = outcome;
    if (outcome != OUT_CRASHED && outcome != OUT_LOST) {
        L->c.v[0] = L->c.v[1] = L->c.v[2] = 0;
        L->c.wp = L->c.wr = 0;
    }
    L->c.throttle = 0;
    ld_push_ev(L, outcome == OUT_LANDED ? LDE_LANDED : outcome == OUT_OFFPAD ? LDE_OFFPAD :
               outcome == OUT_CRASHED ? LDE_CRASHED : LDE_LOST);
}

/* port: the highest corner of that cell: ld_height() is never above it */
static fix cell_max(const Terrain *T, fix x, fix z)
{
    fix h[4], m;
    uint8_t i, j;
    ld_cell_of(T, x, z, &i, &j);
    ter_cell(T, i, j, h);
    m = h[0] > h[1] ? h[0] : h[1];
    if (h[2] > m) m = h[2];
    return h[3] > m ? h[3] : m;
}

/* touchdown: any foot or the bell at or below the ground decides it */
static fix fs[5][3];                    /* port: static (zsdcc's stack frames) */
int ld_contact(Level *L)
{
    Craft *c = &L->c;
    const LdWorld *W = &ld_worlds[L->world];
    fix pen = 0, vs, hs, tilt, t;
    int k, touching = 0, pad = -1;
    LdResult *res = &L->result;
    ld_feet(c, fs);
    for (k = 0; k < 5; k++) {
        fix g, fy = fs[k][1];
        if (fy > cell_max(&L->T, fs[k][0], fs[k][2])) continue;     /* port: above its cell: not touching */
        g = ld_height(&L->T, fs[k][0], fs[k][2]);
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
        ld_finish(L, OUT_LANDED);
    } else if (res->vs_ok && res->hs_ok && res->tilt_ok && res->slope >= FX(0.97030)) {   /* <= 14 degrees */
        res->score = 25;
        ld_finish(L, OUT_OFFPAD);
    } else {
        ld_finish(L, OUT_CRASHED);
    }
    return 1;
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
    ld_gm_ok = 0;                           /* (a new ground) */
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
    L->T.range_page = (uint8_t)lv_field(r, LVF_RANGE_PAGE);
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
