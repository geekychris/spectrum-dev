// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * world.c - ScAIry Fun's simulation, one step per 1/25 s: the car, the
 * autopilot (ai_think / ai_steer), traffic, the hazards, damage and the
 * score, and the route -> arrival -> software update -> next route loop.
 *
 * port: the original world.c in three files, the code otherwise as it was.
 * Built for the Z80 it is 32K of code, too much for the Next's 64K address
 * space beside the renderer, so two parts run from 16K banks that the game
 * maps at 0x0000-0x7FFF for each simulation step (sim_map() in main.c):
 *   world.c          (resident) the tables, events, damage, the hazards
 *                    ahead, cones, and the route and game flow (world_update)
 *   world_traffic.c  (bank, 0x0000) objects, hazards, traffic, collisions
 *   world_ai.c       (bank, 0x4000) the autopilot and the car
 */
#include <string.h>
#include "sc.h"
#include "world_int.h"

const char *const msg_text[MSG_COUNT] = {
    "", "AUTOPILOT ENGAGED", "AUTOPILOT DISENGAGED", "I HAD THAT.",
    "GOOD CATCH.", "RECALCULATING...", "THAT IS A ROAD. PROBABLY.",
    "OBSTACLE DETECTED!", "KEEP YOUR HANDS ON THE WHEEL", "OOPS.",
    "UPDATE INSTALLED. I AM BETTER NOW.", "AUTOPILOT UNAVAILABLE",
    "WELCOME TO SCAIRY FUN", "WATER DETECTED. PROCEEDING ANYWAY.",
    "YOU HAVE ARRIVED.", "CAMERA BLINDED", "I AM VERY PATIENT.",
    "TICKET: FAILED TO YIELD", "TOW TRUCK ON ITS WAY", "POLICY CANCELLED"
};

const char *const ev_name[EV_COUNT] = {
    "none", "ap_on", "ap_off", "unavailable", "save", "needless", "scrape",
    "cone", "debris", "rearend", "crash", "splash", "roll", "ticket",
    "respawn", "phantom", "blinded", "aquaplane", "arrived", "game_over",
    "mult_up", "siren", "route_start", "hit_animal", "say", "win"
};

const char *const hz_names[HZ_COUNT] = {
    "none", "flood", "ghost", "phantom", "cones", "glare", "siren", "debris",
    "deer", "cyclist", "tractor", "merge", "goats", "peds"
};

static const char *const obj_names[OBJ_COUNT] = {
    "none", "car", "truck", "tractor", "cyclist", "deer", "cone", "debris",
    "ambulance", "goat", "pedestrian"
};

/* half length, half width, units */
static const uint8_t obj_size[OBJ_COUNT][2] = {
    { 0, 0 }, { 36, 15 }, { 90, 20 }, { 30, 16 }, { 14, 5 }, { 16, 6 },
    { 4, 4 }, { 10, 10 }, { 50, 17 }, { 10, 4 }, { 5, 5 }
};

const char *hz_name(int type) { return type >= 0 && type < HZ_COUNT ? hz_names[type] : "?"; }
const char *obj_name(int type) { return type >= 0 && type < OBJ_COUNT ? obj_names[type] : "?"; }
int obj_len(int type) { return obj_size[type][0]; }
int obj_wid(int type) { return obj_size[type][1]; }


int32_t z_minus(int32_t a, int32_t b) { return a - b; }

void push(World *w, int type, int32_t a, int32_t b)   /* port: int32_t a, b */
{
    if (w->nev < MAX_EVENTS) {
        w->ev[w->nev].type = type;
        w->ev[w->nev].a = a;
        w->ev[w->nev].b = b;
        w->nev++;
    }
}

int world_pop_event(World *w, Event *e)
{
    if (!w->nev) return 0;
    *e = w->ev[0];
    memmove(&w->ev[0], &w->ev[1], (w->nev - 1) * sizeof(Event));
    w->nev--;
    return 1;
}

void world_say(World *w, int msg)
{
    w->msg = msg;
    w->msg_timer = 75;
    push(w, EV_SAY, msg, 0);
}

void world_defaults(Tune *t)
{
    t->god_mode = 0;
    t->ai_perfect = 0;
    t->traffic_pct = 100;
    t->speed_pct = 100;
    t->mult_secs = 8;
    t->aqua_kmh = 50;
}

/* ---------- damage ---------- */
void hurt(World *w, int n, int ev)
{
    w->incident = 1;
    if (w->tune.god_mode || w->state != GS_PLAY) n = 0;
    w->cover -= n;
    if (w->cover < 0) w->cover = 0;
    push(w, ev, n, w->cover);
    if (w->cover == 0 && w->state == GS_PLAY) {
        w->state = GS_GAMEOVER;
        w->state_timer = 50;
        w->ai.on = 0;
        push(w, EV_GAME_OVER, w->score, w->route);
        world_say(w, MSG_CANCELLED);
    }
}

int cone_at(const World *w, int seg, int16_t *d)
{
    const Seg *g;
    int start, i, lane;
    if (seg < 0 || seg >= w->nseg) return 0;
    g = &w->seg[seg];
    if (!(g->flags & SF_CONES)) return 0;
    for (lane = 0; lane < 8 && !(g->closed & (1 << lane)); lane++)
        ;
    for (start = seg; start > 0 && (w->seg[start - 1].flags & SF_CONES); start--)
        ;
    i = seg - start;
    if (i < 3) {                        /* the taper, across the lane */
        *d = lane * LANE_W + ((i + 1) * LANE_W) / 4;
        return 1;
    }
    if (i % 3 == 0) {
        *d = lane_centre(w, lane);
        return 1;
    }
    return 0;
}

int world_hazard_ahead(const World *w, int segs)
{
    int cs = seg_of(w->car.z), i;
    for (i = 0; i < w->nhaz; i++) {
        const Hazard *h = &w->haz[i];
        if (!HZ_FAILS(h->type) || w->haz_state[i] == 2) continue;
        if (h->type == HZ_SIREN) {
            if (w->haz_state[i] == 1) return i;
            continue;
        }
        if (h->seg - cs <= segs && cs <= h->seg + h->len + 1) return i;
    }
    return -1;
}

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

int32_t world_ap_pct(const World *w)
{
    if (w->dist_all <= 0) return 100;
    return (int32_t)(((w->dist_ap >> 8) * 100) / ((w->dist_all >> 8) ? (w->dist_all >> 8) : 1));
}

static void arrive(World *w)
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

static void score_update(World *w)
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

void world_update(World *w, unsigned in)
{
    unsigned edge = in & ~w->last_in, raw = in;
    int cs, k, g;

    w->frame++;
    if (w->msg_timer > 0 && --w->msg_timer == 0) w->msg = MSG_NONE;
    if (w->state_timer > 0) w->state_timer--;

    switch (w->state) {
    case GS_TITLE:
        if (edge & (IN_FIRE | IN_START)) { world_start_game(w); w->last_in = in; return; }
        if (seg_of(w->car.z) >= w->finish - 2) place_car(w);
        break;
    case GS_INTRO:
        if (w->state_timer == 0 || (edge & (IN_FIRE | IN_START))) {
            w->state = GS_PLAY;
            edge = 0;
            in &= ~IN_FIRE;
        }
        break;
    case GS_ARRIVED:
        if (w->state_timer == 0 || (w->state_timer < 210 && (edge & (IN_FIRE | IN_START)))) {
            if (w->route < NUM_ROUTES - 1) {
                w->state = GS_UPDATE;
                w->state_timer = 200;
                world_say(w, MSG_UPDATED);
            } else {
                w->state = GS_WIN;
                w->state_timer = 50;
                push(w, EV_WIN, w->score, 0);
            }
        }
        break;
    case GS_UPDATE:
        if (w->state_timer == 0 || (w->state_timer < 160 && (edge & (IN_FIRE | IN_START))))
            world_start_route(w, w->route + 1);
        w->last_in = in;
        return;
    case GS_GAMEOVER:
    case GS_WIN:
        if (w->state_timer == 0 && (edge & (IN_FIRE | IN_START))) {
            world_init(w);
            w->last_in = in;
            return;
        }
        break;
    }

    /* the sun: glare builds up over the segments ahead */
    cs = seg_of(w->car.z);
    g = 0;
    for (k = 0; k < 4 && cs + k < w->nseg; k++)
        if (w->seg[cs + k].flags & SF_GLARE) { g = 16; break; }
    if (w->glare < g) w->glare++;
    else if (w->glare > g) w->glare--;

    if (w->state == GS_PLAY || w->state == GS_TITLE) {
        hazards_update(w);
        traffic_spawn(w);
    }
    ai_think(w);
    car_update(w, w->state == GS_PLAY ? in : 0);
    objects_update(w);
    collide(w);
    score_update(w);

    if (w->state == GS_PLAY && seg_of(w->car.z) >= w->finish && w->car.state == CS_DRIVING)
        arrive(w);
    w->last_in = raw;                   /* the press that started the route isn't a new one next frame */
}
