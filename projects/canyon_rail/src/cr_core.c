// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * cr_core.c - Canyon Rail's rules: the ride's line and the canyon, the
 * camera on it, targets and where they are, bolts flying at the camera,
 * the sight and what it hits (through the engine's own projection), the
 * multiplier, the shield, the guardian at each stage's end, and the
 * autopilot (the most urgent thing first: a bolt about to land, then the
 * nearest target). Fixed point throughout, so the host, the classic Amiga
 * and AmigaOS 4 play a seed the same way.
 *
 * port (ZX Spectrum Next): the original's code; its changes are marked
 * port:. zsdcc's int is 16 bits: three products that pass it are widened.
 * The bolt's 64-bit FMUL / FDIV are their exact 32-bit equivalents.
 * tools/check_trace.sh checks whole autopilot games against the original.
 * port: where things are (the ride's line, the camera, the projection,
 * targets', nodes' and bolts' places) is in cr_place.c, resident: the
 * picture uses it too, and these rules are in a bank (see main.c).
 */
#include <string.h>
#include "cr.h"


#define BOLT_SPEED 12                   /* units a tick, on top of the camera's */
#define VIEW_AHEAD 1500
#define FOV        200
#define NODE_R     110
#define BOSS_AHEAD 450

static void event(CRGame *g, int type, int a, int b)
{
    if (g->nev < CR_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}


static uint32_t rnd(CRGame *g)
{
    uint32_t t = (g->rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}


void cr_load_stage(CRGame *g, int stage)
{
    int k, n = 18 + 6 * stage;
    g->stage = stage;
    g->cam_s = 0;
    g->boss = 0;
    g->boss_angle = 0;
    g->boss_cool = 60;
    for (k = 0; k < CR_NODES; k++) g->node_hp[k] = 3 + stage;
    memset(g->bolt, 0, sizeof(g->bolt));
    g->ntg = n < CR_MAX_TARGETS ? n : CR_MAX_TARGETS;
    for (k = 0; k < g->ntg; k++) {
        Target *t = &g->tg[k];
        uint32_t r = rnd(g) % 100;
        t->type = r < 45 ? TG_TURRET : r < 45 + 30 + stage * 3 ? TG_DRONE : TG_POD;
        t->alive = 1;
        t->hp = t->type == TG_TURRET ? 2 : 1;
        t->s = 700 + (int32_t)k * (CR_LEN - 1100) / g->ntg + (int32_t)(rnd(g) % 120);   /* port: (int32_t) */
        t->side = (rnd(g) & 1) ? 1 : -1;
        t->h = 40 + (int32_t)(rnd(g) % 140);
        t->amp = 60 + (int32_t)(rnd(g) % 80);
        t->phase = (int32_t)(rnd(g) & 0xFFFF);
        t->cool = 20 + (int)(rnd(g) % 60);
    }
}

void cr_new_game(CRGame *g, uint32_t seed, int stage)
{
    memset(g, 0, sizeof(*g));
    g->seed = g->rng = seed;
    g->shield = CR_SHIELD;
    g->sx = CR_VIEW_W / 2;
    g->sy = CR_VIEW_H / 2;
    if (stage < 1) stage = 1;
    if (stage > CR_STAGES) stage = CR_STAGES;
    cr_load_stage(g, stage);
}

/* a bolt from (x, y, z) at the camera, aimed where the camera will be */
static void fire_bolt(CRGame *g, int32_t x, int32_t y, int32_t z)
{
    int k, flight;
    int32_t ahead = z - g->cam_s, speed = (g->boss ? 0 : CR_SPEED) + BOLT_SPEED, s1;
    for (k = 0; k < CR_MAX_BOLTS; k++) if (!g->bolt[k].alive) break;
    if (k == CR_MAX_BOLTS || ahead < 60) return;
    flight = (int)(ahead / speed);
    if (flight < 8) flight = 8;
    s1 = g->cam_s + (g->boss ? 0 : CR_SPEED * flight);
    g->bolt[k].alive = 1;
    g->bolt[k].t = 0;
    g->bolt[k].flight = flight;
    g->bolt[k].x0 = x * 65536; g->bolt[k].y0 = y * 65536; g->bolt[k].z0 = z * 65536;
    g->bolt[k].x1 = cr_path_x(s1) * 65536; g->bolt[k].y1 = (cr_path_y(s1) + 40) * 65536; g->bolt[k].z1 = s1 * 65536;
    event(g, CRE_FIRE, k, 0);
}

static int mult(const CRGame *g) { int m = 1 + g->combo / 4; return m > 8 ? 8 : m; }

static void hurt(CRGame *g)
{
    g->shield -= CR_BOLT_DMG;
    g->hurt++;
    g->combo = 0;
    event(g, CRE_HURT, g->shield, 0);
    if (g->shield <= 0) { g->shield = 0; g->over = 1; event(g, CRE_OVER, (int)g->score, 0); }
}

/* the shot: the nearest thing the sight is on */
static void shoot(CRGame *g)
{
    int k, best = -1, kind = 0, px, py;
    int32_t bd = 0x7FFFFFFF, x, y, z, d;
    g->shots++;
    g->fire_cool = CR_FIRE_GAP;
    event(g, CRE_SHOT, g->sx, g->sy);
#define TRY(K, KIND, SIZE) do { \
        if (cr_project(g, x, y, z, &px, &py, &d)) { \
            int32_t r = (SIZE) * FOV / d + 3, ddx = px - g->sx, ddy = py - g->sy; \
            if (ddx * ddx + ddy * ddy <= r * r && d < bd) { bd = d; best = (K); kind = (KIND); } \
        } } while (0)
    for (k = 0; k < CR_MAX_BOLTS; k++) if (g->bolt[k].alive) { cr_bolt_pos(g, k, &x, &y, &z); TRY(k, 1, 9); }
    if (g->boss) {
        for (k = 0; k < CR_NODES; k++) if (g->node_hp[k] > 0) { cr_node_pos(g, k, &x, &y, &z); TRY(k, 2, 22); }
    } else {
        for (k = 0; k < g->ntg; k++) {
            int32_t ahead = g->tg[k].s - g->cam_s;
            if (!g->tg[k].alive || ahead < 30 || ahead > VIEW_AHEAD) continue;
            cr_target_pos(g, k, &x, &y, &z);
            TRY(k, 0, cr_size(g->tg[k].type));
        }
    }
#undef TRY
    g->last_shot_hit = best >= 0;
    if (best < 0) { g->combo = 0; event(g, CRE_MISS, 0, 0); return; }
    g->hits++;
    g->combo++;
    if (g->combo > g->best_combo) g->best_combo = g->combo;
    if (kind == 1) {
        g->bolt[best].alive = 0;
        g->score += 25 * mult(g);
        event(g, CRE_BOLT_DOWN, best, 0);
    } else if (kind == 2) {
        if (--g->node_hp[best] > 0) { event(g, CRE_HIT, best, g->node_hp[best]); return; }
        g->score += 500 * mult(g);
        event(g, CRE_NODE, best, 0);
        for (k = 0; k < CR_NODES; k++) if (g->node_hp[k] > 0) return;
        g->score += 2000 * g->stage;
        g->between = CR_BETWEEN;
        memset(g->bolt, 0, sizeof(g->bolt));
        event(g, CRE_BOSS_DOWN, g->stage, 0);
    } else {
        Target *t = &g->tg[best];
        if (--t->hp > 0) { event(g, CRE_HIT, best, t->hp); return; }
        t->alive = 0;
        g->kills++;
        if (t->type == TG_POD) {
            g->shield = g->shield + 25 > CR_SHIELD ? CR_SHIELD : g->shield + 25;
            g->score += 50 * mult(g);
            event(g, CRE_POD, best, g->shield);
        } else {
            g->score += (t->type == TG_TURRET ? 100 : 150) * mult(g);
            event(g, CRE_KILL, best, t->type);
        }
    }
}

void cr_tick(CRGame *g, unsigned in)
{
    int k;
    if (g->over || g->won) return;
    g->tick++;
    if (g->between > 0) {
        if (--g->between == 0) {
            if (g->stage >= CR_STAGES) { g->won = 1; event(g, CRE_WIN, (int)g->score, 0); return; }
            cr_load_stage(g, g->stage + 1);
            event(g, CRE_STAGE, g->stage, 0);
        }
        return;
    }
    /* the ride, up to the guardian */
    if (!g->boss) {
        g->cam_s += CR_SPEED;
        if (g->cam_s >= CR_LEN) { g->cam_s = CR_LEN; g->boss = 1; event(g, CRE_BOSS, g->stage, 0); }
    }
    /* the sight */
    if (in & CR_IN_LEFT) g->sx -= CR_SIGHT_SPEED;
    if (in & CR_IN_RIGHT) g->sx += CR_SIGHT_SPEED;
    if (in & CR_IN_UP) g->sy -= CR_SIGHT_SPEED;
    if (in & CR_IN_DOWN) g->sy += CR_SIGHT_SPEED;
    if (g->sx < 8) g->sx = 8;
    if (g->sx > CR_VIEW_W - 8) g->sx = CR_VIEW_W - 8;
    if (g->sy < 8) g->sy = 8;
    if (g->sy > CR_VIEW_H - 8) g->sy = CR_VIEW_H - 8;
    if (g->fire_cool > 0) g->fire_cool--;
    if ((in & CR_IN_FIRE) && g->fire_cool == 0) shoot(g);
    g->prev_in = in;
    if (g->between) return;
    /* the enemies fire */
    if (!g->boss) {
        for (k = 0; k < g->ntg; k++) {
            Target *t = &g->tg[k];
            int32_t ahead = t->s - g->cam_s, x, y, z;
            if (!t->alive || t->type == TG_POD) continue;
            if (ahead < 20) { t->alive = 0; continue; }     /* passed by */
            if (ahead > 1100 || ahead < 250) continue;
            if (--t->cool > 0) continue;
            t->cool = 70 - g->stage * 10 + (int)(rnd(g) % 40);
            cr_target_pos(g, k, &x, &y, &z);
            fire_bolt(g, x, y, z);
        }
    } else {
        g->boss_angle += 300 + g->stage * 60;
        if (--g->boss_cool <= 0) {
            int32_t x, y, z;
            int n = (int)(rnd(g) % CR_NODES), j;
            for (j = 0; j < CR_NODES && g->node_hp[n] <= 0; j++) n = (n + 1) % CR_NODES;
            cr_node_pos(g, n, &x, &y, &z);
            fire_bolt(g, x, y, z);
            g->boss_cool = 46 - g->stage * 8;
        }
    }
    /* the bolts */
    for (k = 0; k < CR_MAX_BOLTS; k++) {
        Bolt *b = &g->bolt[k];
        if (!b->alive) continue;
        if (++b->t >= b->flight) { b->alive = 0; hurt(g); if (g->over) return; }
    }
}

/* ---------- the autopilot ---------- */
unsigned cr_bot(const CRGame *g)
{
    int k, px = 0, py = 0, found = 0, r = 0;
    int32_t x, y, z, d, best = 0x7FFFFFFF;
    unsigned in = 0;
    if (g->over || g->won || g->between) return 0;
    /* a bolt about to land first */
    for (k = 0; k < CR_MAX_BOLTS; k++) {
        const Bolt *b = &g->bolt[k];
        int qx, qy;
        if (!b->alive) continue;
        cr_bolt_pos(g, k, &x, &y, &z);
        if (!cr_project(g, x, y, z, &qx, &qy, &d)) continue;
        if (b->flight - b->t < best) { best = b->flight - b->t; px = qx; py = qy; r = 9 * FOV / d + 3; found = 1; }
    }
    if (!found || best > 28) {
        int32_t near = 0x7FFFFFFF;
        if (g->boss) {
            for (k = 0; k < CR_NODES; k++) {
                int qx, qy;
                int32_t dd;
                if (g->node_hp[k] <= 0) continue;
                cr_node_pos(g, k, &x, &y, &z);
                if (!cr_project(g, x, y, z, &qx, &qy, &d)) continue;
                dd = (int32_t)(qx - g->sx) * (qx - g->sx) + (int32_t)(qy - g->sy) * (qy - g->sy);    /* port: (int32_t) */
                if (dd < near) { near = dd; px = qx; py = qy; r = 22 * FOV / d + 3; found = 1; }
            }
        } else {
            for (k = 0; k < g->ntg; k++) {
                int32_t ahead = g->tg[k].s - g->cam_s;
                int qx, qy;
                if (!g->tg[k].alive || ahead < 60 || ahead > 1100) continue;
                cr_target_pos(g, k, &x, &y, &z);
                if (!cr_project(g, x, y, z, &qx, &qy, &d)) continue;
                if (qx < 4 || qx > CR_VIEW_W - 4 || qy < 4 || qy > CR_VIEW_H - 4) continue;
                if (ahead < near) { near = ahead; px = qx; py = qy; r = cr_size(g->tg[k].type) * FOV / d + 3; found = 1; }
            }
        }
    }
    if (!found) return 0;
    if (px > g->sx + 3) in |= CR_IN_RIGHT;
    if (px < g->sx - 3) in |= CR_IN_LEFT;
    if (py > g->sy + 3) in |= CR_IN_DOWN;
    if (py < g->sy - 3) in |= CR_IN_UP;
    if ((int32_t)(px - g->sx) * (px - g->sx) + (int32_t)(py - g->sy) * (py - g->sy) <= (int32_t)(r - 2) * (r - 2) &&
        g->fire_cool == 0) in |= CR_IN_FIRE;                    /* port: (int32_t) */
    return in;
}

uint32_t cr_state_hash(const CRGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    const int32_t v[] = { g->score, g->stage, g->cam_s, g->boss, g->sx, g->sy, g->shield, g->combo, g->shots, g->hits,
                          g->kills, g->hurt, g->tick, g->over, g->won, g->node_hp[0], g->node_hp[1], g->node_hp[2], g->node_hp[3] };
    for (i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) { h ^= (uint32_t)v[i]; h *= 16777619UL; }
    for (i = 0; i < g->ntg; i++) { h ^= (uint32_t)(g->tg[i].alive * 16 + g->tg[i].hp); h *= 16777619UL; }
    return h;
}
