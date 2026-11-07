/*
 * wk_core.c - Warden Keep's rules: the original's game.cpp (WardenGame's
 * methods as wk_* functions over the same state), unchanged but for the
 * port's clock and time bonus (marked port:) and the events the Next's
 * sound and test read.
 */
#include <string.h>
#include "wk.h"

void wk_init(GameState *g)
{
    memset(g, 0, sizeof(*g));
    g->mode = GM_TITLE;
}

static void add(GameState *g, uint8_t kind, uint8_t wx, uint8_t wy)
{
    Prop *o;
    if (g->num_objects >= MAX_OBJECTS) return;
    o = &g->objects[g->num_objects++];
    o->kind = kind;
    o->wx = wx;
    o->wy = wy;
    o->anim = 0;
}

void wk_load_room(GameState *g)
{
    static const uint8_t barrel_at[4][2] = { { 2, 2 }, { 5, 2 }, { 3, 5 }, { 5, 5 } };
    static const uint8_t jewel_at[3][2] = { { 6, 3 }, { 2, 6 }, { 6, 6 } };
    uint8_t i;
    memset(g->floor, F_STONE, sizeof(g->floor));
    memset(g->objects, 0, sizeof(g->objects));
    g->num_objects = 0;
    g->score = 0;
    g->jewels_left = 0;
    /* the two back walls (north wy = 0, west wx = 0): the Filmation cutaway */
    for (i = 0; i < WORLD_W; i++) add(g, O_WALL, i, 0);
    for (i = 1; i < WORLD_H; i++) add(g, O_WALL, 0, i);
    g->floor[WORLD_H - 1][WORLD_W - 1] = F_DOOR;      /* the door marker, front-right corner */
    for (i = 0; i < 4; i++) add(g, O_BARREL, barrel_at[i][0], barrel_at[i][1]);
    for (i = 0; i < 3; i++) {
        add(g, O_JEWEL, jewel_at[i][0], jewel_at[i][1]);
        g->jewels_left++;
    }
    g->player.wx = 3;
    g->player.wy = 6;
    g->player.facing = 0;
    g->player.anim = 0;
    g->time_left = WK_TIME;                            /* port */
    g->bonus = 0;
    g->moved = 1;
}

/* the first object in the list on that cell (the original's object_at) */
Prop *wk_object_at(GameState *g, int8_t wx, int8_t wy)
{
    uint8_t i;
    for (i = 0; i < g->num_objects; i++) {
        Prop *o = &g->objects[i];
        if (o->kind == O_NONE) continue;
        if ((int8_t)o->wx == wx && (int8_t)o->wy == wy) return o;
    }
    return 0;
}

static uint8_t blocked(GameState *g, int8_t wx, int8_t wy)
{
    Prop *o;
    if (wx < 0 || wx >= WORLD_W || wy < 0 || wy >= WORLD_H) return 1;
    o = wk_object_at(g, wx, wy);
    if (!o) return 0;
    return o->kind == O_WALL || o->kind == O_BARREL;   /* walls and barrels block, jewels don't */
}

static void pickup_at(GameState *g, int8_t wx, int8_t wy)
{
    Prop *o = wk_object_at(g, wx, wy);
    if (o && o->kind == O_JEWEL) {
        o->kind = O_NONE;
        g->score += 100;
        g->jewels_left--;
        g->events |= EV_JEWEL;
        if (g->jewels_left == 0) {
            g->mode = GM_WIN;
            g->state_timer = 200;
            g->bonus = (int16_t)((g->time_left / 50) * WK_BONUS);   /* port: the time bonus */
            g->score += g->bonus;
            g->events |= EV_WIN;
        }
    }
}

static void try_move(GameState *g, int8_t dx, int8_t dy)
{
    Player *p = &g->player;
    int8_t tx, ty;
    Prop *o;
    if (dx > 0) p->facing = 1;                         /* facing changes even if the step fails */
    else if (dx < 0) p->facing = 3;
    else if (dy < 0) p->facing = 0;
    else if (dy > 0) p->facing = 2;
    g->moved = 1;
    tx = p->wx + dx;
    ty = p->wy + dy;
    o = wk_object_at(g, tx, ty);
    if (o) {
        if (o->kind == O_WALL) { g->events |= EV_BUMP; return; }
        if (o->kind == O_BARREL) {
            int8_t bx = tx + dx, by = ty + dy;
            if (bx < 0 || bx >= WORLD_W || by < 0 || by >= WORLD_H) { g->events |= EV_BUMP; return; }
            if (blocked(g, bx, by)) { g->events |= EV_BUMP; return; }
            o->wx = (uint8_t)bx;                       /* slide the barrel */
            o->wy = (uint8_t)by;
            g->events |= EV_PUSH;
        }
    }
    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) { g->events |= EV_BUMP; return; }
    p->wx = tx;
    p->wy = ty;
    p->anim++;
    g->events |= EV_STEP;
    pickup_at(g, tx, ty);                              /* jewels are taken by walking over them */
}

void wk_tick(GameState *g, uint8_t input)
{
    g->tick++;
    g->events = 0;
    if (g->mode == GM_TITLE) {
        if (input & INPUT_START) {
            wk_load_room(g);
            g->mode = GM_PLAYING;
        }
        return;
    }
    if (g->mode == GM_WIN || g->mode == GM_OVER) {
        if (g->state_timer > 0) g->state_timer--;
        if ((input & INPUT_START) && g->state_timer == 0) g->mode = GM_TITLE;
        return;
    }
    /* GM_PLAYING: one step per input edge (the caller's auto-repeat) */
    if (input & INPUT_UP) try_move(g, 0, -1);
    else if (input & INPUT_DOWN) try_move(g, 0, 1);
    else if (input & INPUT_LEFT) try_move(g, -1, 0);
    else if (input & INPUT_RIGHT) try_move(g, 1, 0);
    /* port: the clock */
    if (g->mode == GM_PLAYING && --g->time_left <= 0) {
        g->time_left = 0;
        g->mode = GM_OVER;
        g->state_timer = 200;
        g->events |= EV_TIMEOUT;
    }
}
