// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * world_setup.c - Terra Strike simulation, part 2: the tunables, spawning,
 * levels, the start of a game and the bridge's scenes - the original's
 * world.c, unchanged, moved here (port: the original's world.c is ~20K of
 * Z80 code, more than one 16K memory bank; this half runs from an 8K page
 * mapped next to it, see src/main.c).
 */
#include "world_int.h"

void world_defaults(Tune *t)
{
    t->god_mode = 0;
    t->inf_fuel = 0;
    t->enemy_fire = 90;
    t->enemy_range = 2200;
    t->turn_rate = 2;
    t->lift = 7;
    t->gravity = 4;
    t->max_speed = 256;
    t->start_lives = 3;
}


int world_spawn(World *w, int type, int32_t x, int32_t z, int yaw)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) {
        Ent *e = &w->ents[i];
        if (e->type == ENT_NONE) {
            x = ts_wrap(x);
            z = ts_wrap(z);
            e->type = type;
            e->hp = type == ENT_BUILDING ? 2 : 1;
            e->yaw = yaw & 255;
            e->timer = -(int)(ts_rand(&w->rng) % 120);
            e->x = F(x);
            e->z = F(z);
            e->y = F(type == ENT_SHIP ? SEA_LEVEL : terrain_height(w, x, z));
            if (is_target(type)) w->targets_left++;
            return i;
        }
    }
    return -1;
}


void world_clear_enemies(World *w)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) w->ents[i].type = ENT_NONE;
    for (i = 0; i < MAX_SHELLS; i++) w->shells[i].active = 0;
    w->targets_left = 0;
}


int world_count_targets(const World *w)
{
    int i, n = 0;
    for (i = 0; i < MAX_ENTS; i++)
        if (is_target(w->ents[i].type)) n++;
    return n;
}


static int site_ok(const World *w, int type, int32_t x, int32_t z)
{
    int32_t px, pz, h;
    int i;

    pad_centre(w, &px, &pz);
    {
        int32_t dx = ts_wrap_delta(x, px), dz = ts_wrap_delta(z, pz);
        if (dx > -3 * TILE && dx < 3 * TILE && dz > -3 * TILE && dz < 3 * TILE) return 0;
    }
    for (i = 0; i < MAX_ENTS; i++) {
        const Ent *e = &w->ents[i];
        int32_t dx, dz;
        if (e->type == ENT_NONE) continue;
        dx = ts_wrap_delta(x, W(e->x));
        dz = ts_wrap_delta(z, W(e->z));
        if (dx > -200 && dx < 200 && dz > -200 && dz < 200) return 0;
    }
    h = terrain_height(w, x, z);
    switch (type) {
    case ENT_SHIP:     return h < SEA_LEVEL - 40;
    case ENT_TANK:     return h > SEA_LEVEL + 40 && h < 600;
    case ENT_BUILDING: return h > SEA_LEVEL + 40 && h < 700;
    case ENT_TREE:     return h > SEA_LEVEL + 50 && h < 800;
    }
    return 0;
}


/* Place `count` entities, the first ones within `near_tiles` of the pad
 * so there is action as soon as the player lifts off. */
static void scatter(World *w, int type, int count, int near_tiles)
{
    int placed = 0, tries = 0;
    int32_t px, pz;
    pad_centre(w, &px, &pz);
    while (placed < count && tries < 600) {
        int32_t x, z;
        int r = (tries < 300 && placed * 2 < count + 1) ? near_tiles : MAP_N / 2;
        x = px + ((int32_t)(ts_rand(&w->rng) % (2 * r * TILE)) - r * TILE);
        z = pz + ((int32_t)(ts_rand(&w->rng) % (2 * r * TILE)) - r * TILE);
        x = ts_wrap(x);
        z = ts_wrap(z);
        tries++;
        if (!site_ok(w, type, x, z)) continue;
        world_spawn(w, type, x, z, (int)(ts_rand(&w->rng) & 255));
        placed++;
    }
}


void world_place_player(World *w, int32_t x, int32_t y, int32_t z, int yaw, int flying)
{
    Craft *p = &w->p;
    p->x = F(ts_wrap(x));
    p->z = F(ts_wrap(z));
    p->y = F(y);
    p->yaw = yaw & 255;
    p->vy = 0;
    p->speed = 0;
    p->bank = 0;
    p->cooldown = 0;
    p->timer = 0;
    p->state = flying ? PS_FLYING : PS_LANDED;
}


void player_to_pad(World *w)
{
    int32_t px, pz;
    pad_centre(w, &px, &pz);
    world_place_player(w, px, w->pad_h + CLEAR, pz, 0, 0);
    w->p.fuel = FUEL_MAX;
    w->p.shield = SHIELD_MAX;
}


void world_start_level(World *w, int level)
{
    int i;
    w->level = level;
    w->state = GS_PLAY;
    w->state_timer = 0;
    world_clear_enemies(w);
    for (i = 0; i < MAX_SHOTS; i++) w->shots[i].active = 0;
    for (i = 0; i < MAX_PARTS; i++) w->parts[i].life = 0;
    w->rng = w->seed * 31 + (uint32_t)level * 977;
    scatter(w, ENT_TREE, 24, 10);
    scatter(w, ENT_BUILDING, 3 + level, 8);
    scatter(w, ENT_TANK, 2 + level, 9);
    scatter(w, ENT_SHIP, 2 + level / 2, MAP_N / 2);
    player_to_pad(w);
}


void world_init(World *w, uint32_t seed)
{
    int i;
    char *b = (char *)w;
    for (i = 0; i < (int)sizeof(World); i++) b[i] = 0;
    world_defaults(&w->tune);
#ifdef __SDCC
    (void)seed;
    terrain_island(w);          /* port: the island made on the host from seed 1987 */
#else
    terrain_generate(w, seed);
#endif
    w->lives = w->tune.start_lives;
    world_start_level(w, 1);
    w->state = GS_TITLE;
}


void world_start_game(World *w)
{
    w->score = 0;
    w->lives = w->tune.start_lives;
    w->nev = 0;
    world_start_level(w, 1);
}


/* ---- scenes: put the game into a known situation (bridge "scene" hook) ---- */
static int nearest(const World *w, int type, int32_t x, int32_t z)
{
    int i, best = -1;
    int32_t bd = 0x7FFFFFFF;
    for (i = 0; i < MAX_ENTS; i++) {
        const Ent *e = &w->ents[i];
        int32_t dx, dz, d;
        if (e->type != type) continue;
        dx = ts_wrap_delta(W(e->x), x);
        dz = ts_wrap_delta(W(e->z), z);
        d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* Hover `dist` units from entity i, facing it, `alt` above the ground. */
static void approach(World *w, int i, int32_t dist, int32_t alt)
{
    Ent *e = &w->ents[i];
    int32_t ex = W(e->x), ez = W(e->z), x, z, g;
    int yaw = (int)(ts_rand(&w->rng) & 255), k;
    /* pick the approach bearing with the lowest ground so we don't start in a hill */
    int best_yaw = yaw;
    int32_t best_g = 0x7FFFFFFF;
    for (k = 0; k < 8; k++, yaw += 32) {
        x = ex - ((TS_SIN(yaw) * dist) >> 16);
        z = ez - ((TS_COS(yaw) * dist) >> 16);
        g = ground_height(w, x, z);
        if (g < best_g) { best_g = g; best_yaw = yaw & 255; }
    }
    x = ex - ((TS_SIN(best_yaw) * dist) >> 16);
    z = ez - ((TS_COS(best_yaw) * dist) >> 16);
    g = ground_height(w, x, z);
    if (g < W(e->y)) g = W(e->y);
    world_place_player(w, x, g + alt, z, best_yaw, 1);
}

int world_scene(World *w, const char *name)
{
    int32_t px, pz;
    int i;
    if (w->state != GS_PLAY) world_start_game(w);
    w->p.fuel = 1000;
    w->p.shield = 100;
    pad_centre(w, &px, &pz);

    if (name[0] == 'p') {                       /* pad */
        world_start_level(w, w->level);
        return 1;
    }
    if (name[0] == 'l' && name[1] == 'a') {     /* landing: hover over the pad */
        world_place_player(w, px, w->pad_h + 140, pz, 0, 1);
        return 1;
    }
    if (name[0] == 'l' && name[1] == 'o') {     /* lowfuel */
        world_place_player(w, px + 4 * TILE, ground_height(w, px + 4 * TILE, pz) + 200, pz, 192, 1);
        w->p.fuel = 160;
        return 1;
    }
    i = nearest(w, name[0] == 's' ? ENT_SHIP : name[0] == 't' ? ENT_TANK :
                   name[0] == 'c' ? ENT_BUILDING : ENT_NONE, px, pz);
    if (i < 0) return 0;
    approach(w, i, 800, 150);
    return 1;
}
