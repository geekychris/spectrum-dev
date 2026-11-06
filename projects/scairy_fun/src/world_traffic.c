// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * world_traffic.c - ScAIry Fun's objects: spawning, the hazards, traffic,
 * how vehicles follow and pass, collisions. In a bank at 0x0000.
 * (port: split out of the original world.c; see world.c)
 */
#include <string.h>
#include "sc.h"
#include "world_int.h"

/* ---------- objects ---------- */
int world_spawn(World *w, int type, int32_t z, int16_t d, int16_t v, int flags)
{
    int i;
    for (i = 0; i < MAX_OBJS; i++) {
        Obj *o = &w->obj[i];
        if (o->type != OBJ_NONE) continue;
        memset(o, 0, sizeof(*o));
        o->type = (uint8_t)type;
        o->flags = (uint8_t)flags;
        o->hz = -1;
        o->z = z;
        o->d = d;
        o->v = o->vt = v;
        o->lane = (d < 0 ? -1 - (int)((-d >> FBITS) / LANE_W) : lane_of(w, d));
        o->colour = (uint8_t)(sc_rand(&w->rng) & 3);
        if (i >= obj_top) obj_top = i + 1;  /* port: the scans' bound */
        return i;
    }
    return -1;
}

/* is lane free of moving things from dz0 to dz1 segments around z? */
int lane_clear(const World *w, int lane, int32_t z, int dz0, int dz1, int skip)
{
    /* port: the loop is lane_scan() (sim_asm.asm) */
    return lane_scan(w->obj, lane, z, dz0 * SEGF, dz1 * SEGF, skip);
}

/* where to pass something small in the lane: on the shoulder if there's
 * room, otherwise to its left */
int16_t swerve_to(const World *w, const Obj *o)
{
    int16_t right = (o->d >> FBITS) + obj_wid(o->type) + CAR_WID + 12;
    if (right + CAR_WID <= road_right(w) + SHOULDER) return right * (1 << FBITS);
    return o->d - ((obj_wid(o->type) + CAR_WID + 12) * (1 << FBITS));
}

/* may object `skip` move into lane at z? (the car counts too) */
static int lane_free_for(const World *w, int lane, int32_t z, int skip)
{
    const Car *c = &w->car;
    int32_t dz = c->z - z;
    if (c->d >= 0 && lane_of(w, c->d) == lane && dz > -3 * SEGF && dz < 3 * SEGF) return 0;
    if (lane < 0 && c->d < 0 && dz > -3 * SEGF && dz < 3 * SEGF) return 0;
    return lane_clear(w, lane, z, -2, 3, skip);
}

static int knocked(const World *w, int seg) { return w->knocked[seg >> 3] & (1 << (seg & 7)); }

static void spawn_hazard(World *w, int i)
{
    const Hazard *h = &w->haz[i];
    const RouteInfo *r = &route_info[w->route];
    Car *c = &w->car;
    int32_t z = (int32_t)h->seg * SEGF;
    int16_t rr = road_right(w);
    int k, o = -1;

    switch (h->type) {
    case HZ_CYCLIST:
        o = world_spawn(w, OBJ_CYCLIST, z, (rr - 10) * (1 << FBITS), FROM_KMH(20), 0);
        break;
    case HZ_DEER:
        o = world_spawn(w, OBJ_DEER, z, (rr + 50) * (1 << FBITS), 0, 0);
        break;
    case HZ_GOATS:
    case HZ_PEDS:
        for (k = 0; k < 3; k++) {
            o = world_spawn(w, h->type == HZ_GOATS ? OBJ_GOAT : OBJ_PED,
                            z + k * (SEGF / 3), (rr + 40 + k * 22) * (1 << FBITS), 0, 0);
            if (o >= 0) w->obj[o].hz = i;
        }
        o = -1;
        break;
    case HZ_TRACTOR:
        o = world_spawn(w, OBJ_TRACTOR, z, lane_centre(w, r->nl - 1) * (1 << FBITS), FROM_KMH(25), 0);
        break;
    case HZ_MERGE:
        o = world_spawn(w, OBJ_TRUCK, z, (rr + 34) * (1 << FBITS), FROM_KMH(60), 0);
        if (o >= 0) w->obj[o].lane = r->nl;     /* on the slip road, for now */
        break;
    case HZ_PHANTOM:
        o = world_spawn(w, OBJ_TRUCK, c->z - 4 * SEGF,
                        lane_centre(w, lane_of(w, c->d)) * (1 << FBITS), c->v + FROM_KMH(15), OF_TAILGATE);
        break;
    case HZ_SIREN:
        o = world_spawn(w, OBJ_AMBULANCE, c->z - 12 * SEGF,
                        lane_centre(w, lane_of(w, c->d)) * (1 << FBITS), c->v + FROM_KMH(40), 0);
        w->siren = 1;
        push(w, EV_SIREN, h->seg, 0);
        break;
    case HZ_DEBRIS: {
        int lane = h->param >= 0 ? h->param : (c->d >= 0 ? lane_of(w, c->d) : r->nl - 1);
        o = world_spawn(w, OBJ_DEBRIS, z, lane_centre(w, lane) * (1 << FBITS), 0, 0);
        break;
    }
    }
    if (o >= 0) w->obj[o].hz = i;
}

static void resolve_hazard(World *w, int i)
{
    w->haz_state[i] = 2;
    if (w->pending_save != i) return;
    if (!w->incident && w->state == GS_PLAY) {
        w->saves++;
        w->total_saves++;
        w->score += 500;
        push(w, EV_SAVE, w->haz[i].type, w->saves);
        world_say(w, MSG_GOOD_CATCH);
    }
    w->pending_save = -1;
}

void hazards_update(World *w)
{
    int cs = seg_of(w->car.z), i;
    for (i = 0; i < w->nhaz; i++) {
        const Hazard *h = &w->haz[i];
        if (w->haz_state[i] == 0) {
            int trig = h->type == HZ_SIREN ? cs >= h->seg : h->seg - cs <= 32;
            if (trig) {
                w->haz_state[i] = 1;
                spawn_hazard(w, i);
            }
        } else if (w->haz_state[i] == 1 && h->type != HZ_SIREN && cs > h->seg + h->len + 2) {
            resolve_hazard(w, i);
        }
    }
}

/* ---------- traffic ---------- */
void traffic_spawn(World *w)
{
    const RouteInfo *r = &route_info[w->route];
    Car *c = &w->car;
    int want = (r->traffic * 70 / 100) * w->tune.traffic_pct / 100;
    int n = 0, i, cs = seg_of(c->z), seg, lane, type;
    int16_t d, v;
    int32_t z;

    {
        const Obj *o = w->obj;
        for (i = 0; i < MAX_OBJS; i++, o++)
            if (o->hz < 0 && (o->type == OBJ_CAR || o->type == OBJ_TRUCK)) n++;
    }
    if (n >= want || (sc_rand(&w->rng) & 7)) return;

    type = (sc_rand(&w->rng) % 5) ? OBJ_CAR : OBJ_TRUCK;
    if (r->no && (sc_rand(&w->rng) % 3) == 0) {
        seg = cs + 30 + (int)(sc_rand(&w->rng) % 6);
        lane = -1 - (int)(sc_rand(&w->rng) % r->no);
        v = -FROM_KMH(r->limit_kmh * (85 + (int)(sc_rand(&w->rng) % 20)) / 100);
    } else if (r->nl > 1 && (sc_rand(&w->rng) % 4) == 0) {
        /* a faster car coming up from behind in a lane we're not in */
        seg = cs - 9;
        lane = (int)(sc_rand(&w->rng) % (r->nl - 1));
        if (c->d >= 0 && lane == lane_of(w, c->d)) return;
        v = FROM_KMH(r->limit_kmh * (110 + (int)(sc_rand(&w->rng) % 15)) / 100);
        type = OBJ_CAR;
    } else {
        seg = cs + 30 + (int)(sc_rand(&w->rng) % 6);
        lane = (int)(sc_rand(&w->rng) % r->nl);
        if (r->nl == 1)                 /* no overtaking here: about the limit */
            v = FROM_KMH(r->limit_kmh * (92 + (int)(sc_rand(&w->rng) % 14)) / 100);
        else
            v = FROM_KMH(r->limit_kmh * (type == OBJ_TRUCK ? 55 : 65 + (int)(sc_rand(&w->rng) % 25)) / 100);
    }
    if (seg < 1 || seg >= w->finish) return;
    if (lane >= 0 && (w->seg[seg].closed & (1 << lane))) return;
    if (lane < 0)                       /* not inside a detour through the oncoming lane */
        for (i = seg - 3; i <= seg + 3; i++)
            if (i >= 0 && i < w->nseg && (w->seg[i].closed & 1) && r->nl == 1) return;
    z = (int32_t)seg * SEGF;
    if (!lane_clear(w, lane, z, -3, 3, -1)) return;
    d = lane_centre(w, lane) * (1 << FBITS);
    i = world_spawn(w, type, z, d, v, v < 0 ? OF_ONCOMING : 0);
    if (i >= 0) w->obj[i].lane = lane;
}

/* the speed history, for the truck that reacts late */
static int16_t car_v_ago(const World *w, int frames)
{
    return w->car.hist[(w->car.hist_pos - frames) & 31];
}

static void vehicle_follow(World *w, Obj *o, int i)
{
    Car *c = &w->car;
    int32_t best = 5 * SEGF, dz;
    int16_t lead_v = 0, gap, des, t = o->vt;
    int k, len = obj_len(o->type), found = 0;

    const Obj *p;
    (void)k; (void)i;
    /* port: the search is lead_scan() (sim_asm.asm) */
    p = lead_scan(w->obj, o, &best);
    if (p) { lead_v = p->v; found = 1; }
    dz = c->z - o->z;
    if (c->state == CS_DRIVING && dz > 0 && dz < best &&
        (iabs16(c->d - o->d) < (40 * (1 << FBITS)) || (c->d >= 0 && lane_of(w, c->d) == o->lane))) {
        best = dz;
        lead_v = c->v;
        found = 1;
    }
    if (found) {
        gap = (int16_t)(best >> FBITS) - len - CAR_LEN;
        des = (o->v * 25) / 16 + 30;
        if (gap < des) {
            int16_t lim = lead_v + (gap - des) * 2;
            if (lim < 0) lim = 0;
            if (lim < t) t = lim;
        }
    }
    if (o->v < t) { o->v += 3; if (o->v > t) o->v = t; }
    else if (o->v > t) { o->v -= 10; if (o->v < t) o->v = t; }
}

static void move_across(Obj *o, int16_t target, int16_t rate)
{
    if (o->d < target) { o->d += rate; if (o->d > target) o->d = target; }
    else if (o->d > target) { o->d -= rate; if (o->d < target) o->d = target; }
}

void objects_update(World *w)
{
    const RouteInfo *r = &route_info[w->route];
    Car *c = &w->car;
    int i;
    Obj *o = w->obj;
    int cd_any;

    /* port: the scans' bound, lowered past slots freed since */
    while (obj_top > 0 && w->obj[obj_top - 1].type == OBJ_NONE) obj_top--;
    /* port: no cyclist or debris now means none this step (nothing turns into
       one or is spawned here), so the vehicles needn't look for them */
    cd_any = obj_find(w->obj, 0, OBJ_BIT(OBJ_CYCLIST) | OBJ_BIT(OBJ_DEBRIS)) < MAX_OBJS;
    for (i = 0; i < MAX_OBJS; i++, o++) {
        int32_t dz;
        if (o->type == OBJ_NONE) continue;
        if (o->cool > 0) o->cool--;
        dz = o->z - c->z;

        switch (o->type) {
        case OBJ_CAR:
        case OBJ_TRUCK:
            if (o->flags & OF_ONCOMING) {
                if (o->flags & OF_KNOCKED) o->v = 0;           /* a wreck */
                else {
                    /* brake for the car in our lane, and wait after a detour
                     * through our lane until the car is through it */
                    int16_t gap = (int16_t)(dz >> FBITS) - obj_len(o->type) - CAR_LEN;
                    int os = seg_of(o->z), cs = seg_of(c->z), k, wait = 0;
                    if (r->nl == 1 && dz > 0)
                        for (k = os - 2; k >= cs && k > os - 40; k--)
                            if (k < w->nseg && (w->seg[k].closed & 1)) {
                                /* k is the zone's far end: wait there while the
                                 * car is about to come through */
                                int32_t stop = (int32_t)(k + 4) * SEGF;
                                int start = k;
                                while (start > 0 && (w->seg[start - 1].closed & 1)) start--;
                                wait = cs >= start - 12 && o->z > stop && o->z - stop < 3 * SEGF;
                                break;
                            }
                    int16_t to = lane_centre(w, o->lane) * (1 << FBITS);
                    int blocking = iabs16(c->d - o->d) < (34 * (1 << FBITS)) && gap > -10 && gap < 4 * SEG_LEN;
                    if (blocking && o->v == 0 && ++o->timer > 50) o->flags |= OF_PASSING;
                    if (o->flags & OF_PASSING) {
                        /* given up waiting: over to the kerb until the car is past */
                        to = (road_left(w) - 26) * (1 << FBITS);
                        if (dz < -2 * SEGF) { o->flags &= ~OF_PASSING; o->timer = 0; }
                        blocking = dz > 0 && gap < 2 * SEG_LEN && iabs16(o->d - to) > (4 * (1 << FBITS));
                    }
                    if (wait || blocking) {
                        o->v += 10;
                        if (o->v > 0) o->v = 0;
                    } else if (!o->cool && o->v > o->vt) {
                        o->v -= 4;
                        if (o->v < o->vt) o->v = o->vt;
                    }
                    move_across(o, to, 16);
                }
                break;
            }
            if (o->flags & OF_TAILGATE) {
                /* sits close behind and reacts to the car half a second late */
                int16_t gap = (int16_t)(-dz >> FBITS) - obj_len(o->type) - CAR_LEN;
                int16_t late = car_v_ago(w, 12), t = late + (gap - 48) * 2;
                if (t > late + FROM_KMH(30)) t = late + FROM_KMH(30);
                if (t < 0) t = 0;
                if (o->v < t) { o->v += 6; if (o->v > t) o->v = t; }
                else if (o->v > t) { o->v -= 12; if (o->v < t) o->v = t; }
                o->d = c->d >= 0 ? lane_centre(w, lane_of(w, c->d)) * (1 << FBITS) : o->d;
                if (o->hz >= 0 && w->haz_state[o->hz] == 2) {
                    o->flags &= ~OF_TAILGATE;   /* off it goes */
                    o->vt = FROM_KMH(r->limit_kmh);
                    o->lane = (r->nl > 1 ? 0 : r->nl - 1);
                }
                break;
            }
            if (o->hz >= 0 && w->haz[o->hz].type == HZ_MERGE && o->lane == r->nl) {
                if (dz > 0 && dz < 8 * SEGF) o->lane = w->haz[o->hz].param;
                break;
            }
            /* round a closed lane: the next lane, or the oncoming one */
            if (o->lane >= 0) {
                int s = seg_of(o->z) + 6;
                if (s < w->nseg && (w->seg[s].closed & (1 << o->lane))) {
                    int to = o->lane > 0 ? o->lane - 1 : -1;
                    if (lane_free_for(w, to, o->z, i)) o->lane = to;
                }
                else if (s < w->nseg && !(w->seg[s].flags & SF_CONES) && o->lane < 0 && !(o->flags & OF_ONCOMING))
                    o->lane = 0;
            }
            vehicle_follow(w, o, i);
            {
                /* round a cyclist, within the lane */
                int16_t to = lane_centre(w, o->lane) * (1 << FBITS);
                int k;
                /* port: only cyclists and debris do anything here, so only they are visited */
                for (k = cd_any ? obj_find(w->obj, 0, OBJ_BIT(OBJ_CYCLIST) | OBJ_BIT(OBJ_DEBRIS)) : MAX_OBJS;
                     k < MAX_OBJS;
                     k = obj_find(w->obj, k + 1, OBJ_BIT(OBJ_CYCLIST) | OBJ_BIT(OBJ_DEBRIS))) {
                    const Obj *p = &w->obj[k];
                    int32_t ahead = p->z - o->z;
                    if (ahead <= -SEGF || ahead >= 6 * SEGF || iabs16(p->d - to) >= (34 * (1 << FBITS)))
                        continue;
                    if (p->type == OBJ_CYCLIST) to = p->d - (34 * (1 << FBITS));
                    else if (p->type == OBJ_DEBRIS) to = swerve_to(w, p);
                }
                move_across(o, to, 20);
            }
            break;

        case OBJ_AMBULANCE:
            if (!(o->flags & OF_PASSING)) {
                /* catch up, then sit behind the car until it moves over */
                int16_t gap = (int16_t)(-dz >> FBITS) - obj_len(o->type) - CAR_LEN;
                if (gap > 120) o->v = c->v + FROM_KMH(40);
                else {
                    o->v = c->v;
                    o->timer++;
                }
                if (iabs16(c->d - o->d) > (45 * (1 << FBITS)) || c->state != CS_DRIVING) {
                    o->flags |= OF_PASSING;
                } else if (o->timer > 75) {
                    /* a ticket, unless the traffic left nowhere to move over to */
                    int lane = c->d >= 0 ? lane_of(w, c->d) : 0, blocked = r->nl > 1;
                    if (blocked && lane > 0 && lane_clear(w, lane - 1, c->z, -2, 3, i)) blocked = 0;
                    if (blocked && lane < r->nl - 1 && lane_clear(w, lane + 1, c->z, -2, 3, i)) blocked = 0;
                    if (blocked) o->timer = 15;     /* and time to move once it can */
                    else {
                        o->flags |= OF_PASSING;
                        hurt(w, 10, EV_TICKET);
                        world_say(w, MSG_TICKET);
                    }
                }
            } else {
                /* out from behind the car first, then away */
                o->v = iabs16(c->d - o->d) < (34 * (1 << FBITS)) && dz < 0 ? c->v : c->v + FROM_KMH(50);
                if (iabs16(c->d - o->d) <= (45 * (1 << FBITS)) && dz < 2 * SEGF) {
                    /* still in the way: round it, by another lane or the oncoming one */
                    if (r->nl > 1) {
                        int lane = c->d >= 0 ? lane_of(w, c->d) : 0;
                        o->lane = (lane > 0 ? lane - 1 : 1);
                    } else {
                        o->lane = -1;
                    }
                }
                move_across(o, lane_centre(w, o->lane) * (1 << FBITS), 24);
                if (dz > 3 * SEGF && o->hz >= 0 && w->haz_state[o->hz] == 1) {
                    w->siren = 0;
                    resolve_hazard(w, o->hz);
                }
            }
            break;

        case OBJ_TRACTOR:
            if (o->hz >= 0 && seg_of(o->z) > w->haz[o->hz].seg + w->haz[o->hz].len) {
                o->d += 20;             /* turns off into a field */
                o->v = FROM_KMH(10);
                if ((o->d >> FBITS) > road_right(w) + 220) o->type = OBJ_NONE;
            } else {
                vehicle_follow(w, o, i);
            }
            break;

        case OBJ_CYCLIST:
            break;

        case OBJ_DEER:
        case OBJ_GOAT:
        case OBJ_PED:
            /* cross when the car is close; a hit one is thrown aside */
            if (o->vd == 0 && dz > 0 && dz < (o->type == OBJ_PED ? 6 : 5) * SEGF && o->timer == 0) {
                o->timer = 1;
                o->vd = -(o->type == OBJ_PED ? 18 : 30);
            }
            o->d += o->vd;
            break;
        }
        o->z += o->v;
        if (o->type == OBJ_NONE) continue;
        dz = o->z - c->z;
        if (dz < -20 * SEGF || dz > 48 * SEGF || seg_of(o->z) >= w->nseg - 1 || o->z < 0) {
            if (o->type == OBJ_AMBULANCE) {
                w->siren = 0;
                if (o->hz >= 0 && w->haz_state[o->hz] == 1) resolve_hazard(w, o->hz);
            }
            o->type = OBJ_NONE;
        }
    }
}

void collide(World *w)
{
    Car *c = &w->car;
    int i, k, cs;

    if (c->state != CS_DRIVING && c->state != CS_AQUAPLANE) return;
    /* port: obj_find visits the slots in use */
    for (i = obj_find(w->obj, 0, OBJ_ANY); i < MAX_OBJS; i = obj_find(w->obj, i + 1, OBJ_ANY)) {
        Obj *o = &w->obj[i];
        int32_t dz;
        int16_t dd;
        int ln, wd;
        if (o->type == OBJ_NONE || o->cool || (o->flags & OF_KNOCKED)) continue;
        dz = z_minus(o->z, c->z) >> FBITS;  /* port: zsdcc dropped a byte of o->z - c->z here */
        dd = (o->d - c->d) >> FBITS;
        ln = obj_len(o->type);
        wd = obj_wid(o->type);
        if (iabs(dz) >= CAR_LEN + ln || iabs16(dd) >= CAR_WID + wd) continue;
        switch (o->type) {
        case OBJ_DEBRIS:
            o->type = OBJ_NONE;
            c->v = c->v * 3 / 4;
            hurt(w, 10, EV_DEBRIS);
            world_say(w, MSG_OOPS);
            break;
        case OBJ_DEER: case OBJ_GOAT: case OBJ_PED: case OBJ_CYCLIST:
            o->cool = 1000;
            o->vd = dd > 0 ? 60 : -60;
            o->v = 0;
            c->v /= 2;
            hurt(w, 20, EV_HIT_ANIMAL);
            world_say(w, MSG_OOPS);
            break;
        default:
            o->cool = 50;
            if (o->v < 0) {
                /* head on: the other car ends up a wreck at the roadside */
                c->v = 0;
                o->v = 0;
                o->flags |= OF_KNOCKED;
                o->d -= 48 * (1 << FBITS);
                hurt(w, 35, EV_CRASH);
            } else if (dz > 0) {
                c->v = o->v * 3 / 4;
                c->z = o->z - ((int32_t)(CAR_LEN + ln + 2) * (1 << FBITS));
                hurt(w, 25, EV_CRASH);
            } else {
                o->v = c->v * 3 / 4;
                o->z = c->z - ((int32_t)(CAR_LEN + ln + 2) * (1 << FBITS));
                o->flags &= ~OF_TAILGATE;
                hurt(w, 15, EV_REAREND);
            }
            world_say(w, MSG_OOPS);
            break;
        }
    }

    /* cones in a closed lane */
    cs = seg_of(c->z);
    for (k = cs; k <= cs + 1; k++) {
        int16_t cd;
        if (k >= w->nseg || knocked(w, k) || !cone_at(w, k, &cd)) continue;
        if (iabs(((int32_t)k * SEGF - c->z) >> FBITS) < CAR_LEN + 4 &&
            iabs16((c->d >> FBITS) - cd) < CAR_WID + 4) {
            w->knocked[k >> 3] |= (uint8_t)(1 << (k & 7));
            c->v = c->v * 7 / 8;
            hurt(w, 2, EV_CONE);
        }
    }
}

