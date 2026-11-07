// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * world_ai.c - ScAIry Fun's autopilot (ai_think, ai_steer, its lane
 * changes) and the car. In a bank at 0x4000.
 * (port: split out of the original world.c; see world.c)
 */
#include <string.h>
#include "sc.h"
#include "world_int.h"

/* ---------- the autopilot ---------- */
int world_ap_allowed(const World *w)
{
    const Car *c = &w->car;
    int lane;
    if (c->state != CS_DRIVING || c->d < 0) return 0;      /* it can pull away from a standstill */
    if (w->state != GS_PLAY && w->state != GS_TITLE) return 0;
    lane = lane_of(w, c->d);
    return iabs16((c->d >> FBITS) - lane_centre(w, lane)) <= LANE_W / 4;
}

static void takeover(World *w)
{
    int h;
    w->ai.on = 0;
    w->takeovers++;
    w->mult = 1;
    w->ap_frames = 0;
    push(w, EV_AP_OFF, w->takeovers, 0);
    if (w->state != GS_PLAY) return;
    h = world_hazard_ahead(w, 24);
    if (h >= 0) {
        w->pending_save = h;
        w->incident = 0;
        world_say(w, MSG_AP_OFF);
    } else {
        w->needless++;
        w->total_needless++;
        push(w, EV_NEEDLESS, w->needless, 0);
        world_say(w, MSG_HAD_THAT);
    }
}

void world_set_ap(World *w, int on)
{
    if (on && !w->ai.on) {
        if (!world_ap_allowed(w)) {
            push(w, EV_UNAVAILABLE, 0, 0);
            world_say(w, MSG_UNAVAILABLE);
            return;
        }
        w->ai.on = 1;
        w->ai.lane = lane_of(w, w->car.d);
        w->ap_frames = 0;
        push(w, EV_AP_ON, 0, 0);
        world_say(w, MSG_AP_ON);
    } else if (!on && w->ai.on) {
        takeover(w);
    }
}

static int perfect(const World *w)
{
    return w->tune.ai_perfect || w->state == GS_TITLE;
}

/* the lane change rules on a multi-lane road */
static void ai_lanes(World *w)
{
    Ai *a = &w->ai;
    Car *c = &w->car;
    const RouteInfo *r = &route_info[w->route];
    int i, cs = seg_of(c->z);

    if (a->lane_hold > 0) { a->lane_hold--; return; }
    if (perfect(w)) {
        /* round a closed lane */
        for (i = 2; i < 14; i++)
            if (a->lane >= 0 && cs + i < w->nseg && (w->seg[cs + i].closed & (1 << a->lane))) {
                int to = a->lane > 0 ? a->lane - 1 : (r->nl > 1 ? 1 : -1), end = cs + i;
                while (end < w->nseg - 1 && (w->seg[end].flags & SF_CONES)) end++;
                if (to < 0 && !lane_clear(w, -1, c->z, -2, end - cs + 1, -1)) return;
                a->lane = to;
                a->lane_hold = 50;
                return;
            }
        if (a->lane < 0 && !(w->seg[cs < w->nseg - 14 ? cs + 13 : cs].flags & SF_CONES) &&
            !(w->seg[cs].flags & SF_CONES)) {
            a->lane = 0;
            a->lane_hold = 50;
            return;
        }
    }
    if (r->nl < 2 || a->lane < 0) return;
    if (perfect(w)) {
        /* move over for an ambulance */
        /* port: obj_find visits only the objects the loops act on */
        for (i = obj_find(w->obj, 0, OBJ_BIT(OBJ_AMBULANCE)); i < MAX_OBJS;
             i = obj_find(w->obj, i + 1, OBJ_BIT(OBJ_AMBULANCE))) {
            const Obj *o = &w->obj[i];
            int32_t dz = o->z - c->z;
            if (!(o->flags & OF_PASSING) && o->lane == a->lane &&
                dz < 0 && dz > -10 * SEGF) {
                int to = a->lane > 0 ? a->lane - 1 : a->lane + 1;
                if (!lane_clear(w, to, c->z, -2, 3, i)) to = a->lane < r->nl - 1 ? a->lane + 1 : -9;
                if (to >= 0 && to < r->nl && lane_clear(w, to, c->z, -2, 3, i)) {
                    a->lane = to;
                    a->lane_hold = 125;
                }
                return;
            }
        }
    }
    /* overtake a slow car ahead, then keep right */
    {
    const Obj *o = w->obj;
    for (i = 0; i < MAX_OBJS; i++, o++) {
        int32_t dz;
        if (o->type == OBJ_NONE || o->lane != a->lane) continue;
        dz = o->z - c->z;
        if (dz <= 0 || dz > 6 * SEGF) continue;
        if (o->type != OBJ_CAR && o->type != OBJ_TRUCK && o->type != OBJ_TRACTOR) continue;
        if (o->v < a->target_v - FROM_KMH(10) && a->lane > 0 &&
            lane_clear(w, a->lane - 1, c->z, -4, 5, -1)) {
            a->lane--;
            a->lane_hold = 100;
            return;
        }
    }
    }
    if (a->lane < r->nl - 1 && lane_clear(w, a->lane + 1, c->z, -3, 7, -1)) {
        for (i = 0; i < 16 && cs + i < w->nseg; i++)
            if (w->seg[cs + i].closed & (1 << (a->lane + 1))) return;
        a->lane++;
        a->lane_hold = 100;
    }
}

void ai_think(World *w)
{
    Ai *a = &w->ai;
    Car *c = &w->car;
    const RouteInfo *r = &route_info[w->route];
    int cs = seg_of(c->z), k, i, perf = perfect(w);
    int16_t limit;
    const Obj *o;

    /* the low sun */
    if (!perf && w->glare >= 12) {
        if (!a->blinded) {
            a->blinded = 1;
            a->blind_z = c->z;
            a->blind_x = (road_cx(w, c->z) * (1 << FBITS)) + c->d;
            a->blind_dir = w->seg[cs < w->nseg ? cs : w->nseg - 1].dir;
            if (a->on) { push(w, EV_BLINDED, cs, 0); world_say(w, MSG_BLINDED); }
        }
    } else if (a->blinded && w->glare < 6) {
        a->blinded = 0;
        if (a->on) world_say(w, MSG_RECALC);
    }

    if (a->on) ai_lanes(w);

    /* the plan: where it means to be at each of the next 16 segments */
    a->plan_from = cs;
    for (k = 0; k < 16; k++) {
        int32_t z = (int32_t)(cs + k) * SEGF, d;
        if (a->blinded) {
            int32_t x = a->blind_x + ((a->blind_dir * (z - a->blind_z)) >> SEG_BITS);
            d = x - (road_cx(w, z) * (1 << FBITS));
            if (d > 32000) d = 32000;       /* port: the plan is int16_t (never this far in play) */
            if (d < -32000) d = -32000;
        } else {
            d = lane_centre(w, a->lane) * (1 << FBITS);
            if (!perf) d += ghost_at(w, z) * (1 << FBITS);
        }
        a->plan[k] = d;
    }
    /* give a cyclist room: move over within the lane to pass */
    for (i = obj_find(w->obj, 0, OBJ_BIT(OBJ_CYCLIST)); i < MAX_OBJS;
         i = obj_find(w->obj, i + 1, OBJ_BIT(OBJ_CYCLIST))) {
        int32_t dz;
        int s0;
        o = &w->obj[i];
        dz = o->z - c->z;
        if (dz < -2 * SEGF || dz > 8 * SEGF || a->blinded) continue;
        s0 = seg_of(o->z) - cs;
        for (k = s0 - 6; k <= s0 + 2; k++)
            if (k >= 0 && k < 16 && iabs16(a->plan[k] - o->d) < (34 * (1 << FBITS)))
                a->plan[k] = o->d - (34 * (1 << FBITS));
    }
    if (perf) {
        /* pull over for an ambulance, swerve round debris */
        for (i = obj_find(w->obj, 0, OBJ_BIT(OBJ_AMBULANCE) | OBJ_BIT(OBJ_DEBRIS)); i < MAX_OBJS;
             i = obj_find(w->obj, i + 1, OBJ_BIT(OBJ_AMBULANCE) | OBJ_BIT(OBJ_DEBRIS))) {
            int32_t dz;
            o = &w->obj[i];
            dz = o->z - c->z;
            if (o->type == OBJ_AMBULANCE && !(o->flags & OF_PASSING) && dz < 0 && dz > -8 * SEGF &&
                r->nl == 1) {
                for (k = 0; k < 16; k++) a->plan[k] += 50 * (1 << FBITS);
            }
            if (o->type == OBJ_DEBRIS && dz > -SEGF && dz < 12 * SEGF &&
                iabs16(o->d - a->plan[0]) < (30 * (1 << FBITS))) {
                int s = seg_of(o->z) - cs;
                int16_t to = swerve_to(w, o);
                for (k = s - 8; k <= s + 1; k++)
                    if (k >= 0 && k < 16) a->plan[k] = to;
            }
        }
    }

    /* what it sees */
    a->nseen = 0;
    if (!a->blinded) {
        for (i = obj_find(w->obj, 0, OBJ_ANY); i < MAX_OBJS && a->nseen < MAX_SEEN;
             i = obj_find(w->obj, i + 1, OBJ_ANY)) {
            int32_t dz;
            o = &w->obj[i];
            dz = o->z - c->z;
            if (dz < -6 * SEGF || dz > 30 * SEGF) continue;
            if (o->type == OBJ_DEBRIS && !perf) continue;      /* too small to matter, it thinks */
            a->seen[a->nseen].z = o->z;
            a->seen[a->nseen].d = o->d;
            a->seen[a->nseen].type = o->type;
            a->seen[a->nseen].phantom = 0;
            a->nseen++;
        }
    }

    /* the overpass shadow: something huge in the lane, it decides */
    if (!perf && a->on && a->phantom_timer == 0) {
        for (k = 1; k <= 6 && cs + k < w->nseg; k++) {
            int s = cs + k;
            if ((w->seg[s].flags & SF_OVERPASS) && !(w->seg[s - 1].flags & SF_OVERPASS) &&
                s != a->phantom_seg) {
                a->phantom_seg = s;
                a->phantom_timer = 30;
                push(w, EV_PHANTOM, s, 0);
                world_say(w, MSG_OBSTACLE);
                break;
            }
        }
    }
    if (a->phantom_timer > 0) {
        if (a->nseen < MAX_SEEN) {
            a->seen[a->nseen].z = (int32_t)a->phantom_seg * SEGF;
            a->seen[a->nseen].d = c->d;
            a->seen[a->nseen].type = OBJ_CAR;
            a->seen[a->nseen].phantom = 1;
            a->nseen++;
        }
        if (--a->phantom_timer == 0 && a->on) world_say(w, MSG_RECALC);
    }

    /* speed */
    limit = FROM_KMH(r->limit_kmh * w->tune.speed_pct / 100);
    if (perf) {
        for (k = 0; k < 9 && cs + k < w->nseg; k++)
            if (w->seg[cs + k].flags & SF_FLOOD) {
                int16_t slow = FROM_KMH(aqua_kmh(w) * 7 / 10);
                if (slow < limit) limit = slow;
                break;
            }
    }
    for (i = 0; i < a->nseen; i++) {
        const Seen *s = &a->seen[i];
        int32_t dz = s->z - c->z;
        int16_t pd, gap, des, ov = 0;
        int idx, len;
        if (dz <= 0 || s->phantom) continue;
        idx = seg_of(s->z) - cs;
        if (idx > 15) idx = 15;
        pd = a->plan[idx < 0 ? 0 : idx];
        len = obj_len(s->type);
        /* in the path, or about to cross it */
        {
            int16_t dd = iabs16(s->d - pd), band = (CAR_WID + obj_wid(s->type) + 6) * (1 << FBITS);
            int hit = dd < band;
            for (k = obj_find(w->obj, 0, OBJ_BIT(s->type)); k < MAX_OBJS && !hit;
                 k = obj_find(w->obj, k + 1, OBJ_BIT(s->type))) {
                o = &w->obj[k];
                if (o->type == s->type && o->z == s->z && o->d == s->d && o->vd) {
                    int16_t later = o->d + o->vd * 45;
                    hit = (o->d > pd + band && later < pd + band) || (o->d < pd - band && later > pd - band);
                    ov = o->v;
                }
            }
            if (!hit) continue;
        }
        for (k = obj_find(w->obj, 0, OBJ_BIT(s->type)); k < MAX_OBJS; k = obj_find(w->obj, k + 1, OBJ_BIT(s->type)))
            if (w->obj[k].z == s->z) { ov = w->obj[k].v; break; }
        if (ov < 0) ov = 0;
        gap = (int16_t)(dz >> FBITS) - len - CAR_LEN;
        if (ov == 0) {
            /* standing or crossing: stop short of it */
            int16_t stop = ((int32_t)c->v * c->v) / (2 * 8 * 16) + 50;     /* port: (int32_t) */
            if (gap < stop + 60) limit = 0;
        } else {
            des = (c->v * 25) / 16 + 30;
            if (gap < des) {
                int16_t t = ov + (gap - des) * 2;
                if (t < 0) t = 0;
                if (t < limit) limit = t;
            }
        }
    }
    if (a->phantom_timer > 0) limit = 0;
    if (perf) {
        /* wait before cones it can't get round yet; creep while moving over */
        int in = c->d >= 0 ? lane_of(w, c->d) : -1;
        for (k = 1; k <= 5 && cs + k < w->nseg; k++) {
            uint8_t cl = w->seg[cs + k].closed;
            if (a->lane >= 0 && (cl & (1 << a->lane))) { limit = 0; break; }
            if (in >= 0 && (cl & (1 << in)) &&
                iabs16(c->d - (lane_centre(w, a->lane) * (1 << FBITS))) > (12 * (1 << FBITS))) {
                if (limit > FROM_KMH(12)) limit = FROM_KMH(12);
                break;
            }
        }
    }
    if (cs >= w->finish) limit = (w->nseg - 6 - cs) > 0 ? FROM_KMH(30) : 0;
    a->target_v = limit;

    /* comments */
    for (i = 0; i < w->nhaz; i++) {
        const Hazard *h = &w->haz[i];
        if (w->haz_said[i] || !a->on || w->state != GS_PLAY) continue;
        if (h->type == HZ_FLOOD && w->route > 0 && cs >= h->seg - 3 && cs < h->seg) {
            w->haz_said[i] = 1;
            world_say(w, MSG_WATER_ANYWAY);
        } else if (h->type == HZ_GHOST && ghost_at(w, c->z + 4 * SEGF) > LANE_W / 2) {
            w->haz_said[i] = 1;
            world_say(w, MSG_ROAD_PROBABLY);
        } else if (h->type == HZ_TRACTOR && cs > h->seg + 6 && cs < h->seg + h->len &&
                   c->v < FROM_KMH(30)) {
            w->haz_said[i] = 1;
            world_say(w, MSG_PATIENT);
        }
    }
}

int16_t ai_steer(const World *w)
{
    const Ai *a = &w->ai;
    const Car *c = &w->car;
    int idx = seg_of(c->z + 2 * SEGF) - a->plan_from;
    int16_t target, k;
    int32_t s;
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    target = a->plan[idx];
    k = a->blinded ? 0 : road_curve(w, c->z);
    s = ((int32_t)k * c->v * 9) / 40 + ((target - c->d) >> FBITS) * 4;     /* port: (int32_t) */
    if (s > 256) s = 256;
    if (s < -256) s = -256;
    return s;
}

/* ---------- the car ---------- */
/* the town's flooded streets are deeper: they aquaplane at lower speed */
int aqua_kmh(const World *w)
{
    return w->route == 3 ? w->tune.aqua_kmh * 3 / 5 : w->tune.aqua_kmh;
}

static void off_road(World *w, int side, int16_t depth, int16_t edge)
{
    Car *c = &w->car;
    const Seg *g = &w->seg[seg_of(c->z) < w->nseg ? seg_of(c->z) : w->nseg - 1];
    int t = g->side[side];
    int rail = g->flags & (side ? SF_RAIL_R : SF_RAIL_L);

    if (rail || t == SIDE_MEDIAN || t == SIDE_WALL) {
        c->d = (edge + (side ? -2 : 2)) * (1 << FBITS);
        c->s = 0;
        if (!c->cool) {
            c->v = c->v * 3 / 4;
            c->cool = 15;
            hurt(w, rail ? 5 : t == SIDE_MEDIAN ? 8 : 10, EV_SCRAPE);
        }
    } else if (t == SIDE_GRASS) {
        if (c->v > FROM_KMH(40)) c->v -= 6;
        if (depth > 160) c->d = (edge + (side ? 160 : -160)) * (1 << FBITS);
    } else if (t == SIDE_TOWN) {
        if (depth > 40) {
            c->d = (edge + (side ? 38 : -38)) * (1 << FBITS);
            if (!c->cool) {
                c->v /= 3;
                c->cool = 25;
                hurt(w, 25, EV_CRASH);
                world_say(w, MSG_OOPS);
            }
        }
    } else if ((t == SIDE_LEVEE || t == SIDE_CANAL) ? depth > 16 : depth > 8) {
        int splash = t == SIDE_LEVEE || t == SIDE_CANAL;
        c->state = splash ? CS_SPLASH : CS_ROLL;
        c->timer = 50;
        c->lat = side ? 24 : -24;
        w->ai.on = 0;
        hurt(w, 40, splash ? EV_SPLASH : EV_ROLL);
        if (w->state == GS_PLAY) world_say(w, MSG_TOW);
    }
}

void car_update(World *w, unsigned in)
{
    Car *c = &w->car;
    Ai *a = &w->ai;
    unsigned edge = in & ~w->last_in;
    int cs = seg_of(c->z);
    int16_t k, pull, le, re, u;
    int playing = w->state == GS_PLAY;

    if (c->cool > 0) c->cool--;
    if (c->state == CS_SPLASH || c->state == CS_ROLL) {
        c->v = c->v * 15 / 16;
        c->z += c->v;
        c->d += c->lat;
        if (--c->timer <= 0) respawn(w);
        return;
    }

    if (playing) {
        if (edge & IN_FIRE) world_set_ap(w, !a->on);
        else if (a->on && (in & (IN_LEFT | IN_RIGHT | IN_DOWN))) world_set_ap(w, 0);
    }

    if (a->on || !playing) {
        int dec = a->phantom_timer > 0 ? 18 : 8;
        c->s = ai_steer(w);
        if (c->v < a->target_v) { c->v += 3; if (c->v > a->target_v) c->v = a->target_v; }
        else if (c->v > a->target_v) { c->v -= dec; if (c->v < a->target_v) c->v = a->target_v; }
    } else {
        if (in & IN_LEFT) { c->s -= 40; if (c->s < -256) c->s = -256; }
        else if (in & IN_RIGHT) { c->s += 40; if (c->s > 256) c->s = 256; }
        else if (c->s > 0) { c->s -= 64; if (c->s < 0) c->s = 0; }
        else if (c->s < 0) { c->s += 64; if (c->s > 0) c->s = 0; }
        if (in & IN_DOWN) c->v -= 12;
        else if (in & IN_UP) c->v += 4;
        else c->v -= 1;
        if (c->v < 0) c->v = 0;
        if (c->v > FROM_KMH(MAX_KMH)) c->v = FROM_KMH(MAX_KMH);
    }

    k = road_curve(w, c->z);
    pull = ((int32_t)k * c->v * c->v) / 8000;                /* port: (int32_t) */
    if (cs < w->nseg && (w->seg[cs].flags & SF_FLOOD) && c->v > FROM_KMH(aqua_kmh(w))) {
        if (c->state != CS_AQUAPLANE) push(w, EV_AQUAPLANE, KMH(c->v), 0);
        c->state = CS_AQUAPLANE;
        c->lat = -((c->v - FROM_KMH(aqua_kmh(w))) * 3) / 2;
        c->v -= 1;
    } else {
        c->state = CS_DRIVING;
        c->lat = ((int32_t)c->s * c->v) / 1800 - pull;      /* port: (int32_t) */
    }
    c->d += c->lat;
    c->z += c->v;

    u = c->d >> FBITS;
    le = road_left(w) - SHOULDER;
    re = road_right(w) + SHOULDER;
    if (u < le) off_road(w, 0, le - u, le);
    else if (u > re) off_road(w, 1, u - re, re);

    c->hist[c->hist_pos & 31] = c->v;
    c->hist_pos++;
}

