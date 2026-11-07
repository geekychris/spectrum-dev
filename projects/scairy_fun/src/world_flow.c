// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * world_flow.c - starting a route and the game, arriving, the score.
 * (port: split out of the original world.c; in bank 16, see world.c)
 */
#include <string.h>
#include "sc.h"
#include "world_int.h"

/* ---------- route and game flow ---------- */
void place_car(World *w)
{
    const RouteInfo *r = &route_info[w->route];
    Car *c = &w->car;
    memset(c, 0, sizeof(*c));
    c->z = 3 * SEGF;
    c->d = lane_centre(w, r->nl - 1) * (1 << FBITS);
    c->v = FROM_KMH(r->limit_kmh * 7 / 10);
    memset(&w->ai, 0, sizeof(w->ai));
    w->ai.on = 1;
    w->ai.lane = r->nl - 1;
    w->ai.phantom_seg = -1;
}

void world_start_route(World *w, int route)
{
    route_build(w, route);
    memset(w->obj, 0, sizeof(w->obj));
    memset(w->knocked, 0, sizeof(w->knocked));
    memset(w->haz_said, 0, sizeof(w->haz_said));
    place_car(w);
    w->mult = 1;
    w->ap_frames = 0;
    w->ap_rem = 0;
    w->dist_ap = w->dist_all = 0;
    w->saves = w->needless = w->takeovers = 0;
    w->pending_save = -1;
    w->incident = 0;
    w->route_bonus = 0;
    w->glare = 0;
    w->siren = 0;
    w->state = GS_INTRO;
    w->state_timer = 100;
    push(w, EV_ROUTE_START, route, 0);
}

void world_start_game(World *w)
{
    w->score = 0;
    w->cover = 100;
    w->total_saves = w->total_needless = 0;
    world_start_route(w, 0);
    world_say(w, MSG_HANDS);
}

void world_init(World *w)
{
    Tune t = w->tune;
    int keep = w->tune.speed_pct != 0;
    memset(w, 0, sizeof(*w));
    if (keep) w->tune = t; else world_defaults(&w->tune);
    w->rng = 1234567;
    world_start_route(w, 0);
    w->nev = 0;
    w->state = GS_TITLE;
    w->cover = 100;
}

void arrive(World *w)
{
    int32_t pct = world_ap_pct(w);
    w->route_bonus = pct * 50 + w->saves * 500;
    w->score += w->route_bonus;
    w->cover += 30;
    if (w->cover > 100) w->cover = 100;
    w->state = GS_ARRIVED;
    w->state_timer = 250;
    w->ai.on = 1;
    w->ai.blinded = 0;
    w->pending_save = -1;
    push(w, EV_ARRIVED, w->route, w->score);
    world_say(w, MSG_ARRIVED);
}

void score_update(World *w)
{
    Car *c = &w->car;
    int32_t m;
    if (w->state != GS_PLAY || c->state == CS_SPLASH || c->state == CS_ROLL) return;
    w->dist_all += c->v;
    if (!w->ai.on) return;
    w->dist_ap += c->v;
    w->ap_rem += c->v * w->mult;
    w->score += w->ap_rem >> 8;
    w->ap_rem &= 255;
    w->ap_frames++;
    m = 1 + w->ap_frames / (w->tune.mult_secs * 25);
    if (m > 8) m = 8;
    if (m > w->mult) {
        w->mult = m;
        push(w, EV_MULT_UP, m, 0);
    }
}

/* (port: from the car code in world_ai.c, for the banks' sizes) */
void respawn(World *w)
{
    Car *c = &w->car;
    const RouteInfo *r = &route_info[w->route];
    int s = seg_of(c->z) - 4, i;

    while (s > 2 && (w->seg[s].flags & (SF_FLOOD | SF_GHOST | SF_CONES | SF_GLARE))) s--;
    /* and well back from the start of whatever caught it, so there's room
     * to get going and take it by hand */
    for (i = 0; i < w->nhaz; i++)
        if (w->haz[i].seg >= s && w->haz[i].seg < s + 10) s = w->haz[i].seg - 10;
    if (s < 2) s = 2;
    c->z = (int32_t)s * SEGF;
    c->d = lane_centre(w, r->nl - 1) * (1 << FBITS);
    c->v = c->s = c->lat = 0;
    c->state = CS_DRIVING;
    c->cool = 0;
    w->ai.on = 0;
    w->ai.lane = r->nl - 1;
    w->ai.blinded = 0;
    w->mult = 1;
    w->ap_frames = 0;
    for (i = 0; i < MAX_OBJS; i++) {
        Obj *o = &w->obj[i];
        if (o->type != OBJ_NONE && iabs(o->z - c->z) < 4 * SEGF && o->type != OBJ_DEBRIS &&
            o->type != OBJ_AMBULANCE) o->type = OBJ_NONE;
    }
    push(w, EV_RESPAWN, s, 0);
}

