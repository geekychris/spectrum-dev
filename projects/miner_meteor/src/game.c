/*
 * Miner Meteor rules - the original's game.cpp (MinerGame methods) as C
 * over one global GameState. Changes are marked `port:`; none changes how
 * the game plays.
 */
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "levels_data.h"
#include "tmap.h"
#include "plat.h"

GameState gs;

/* solid_at() of the original: walls, collapsing floors and conveyors */
static const uint8_t solid_kinds[T_KIND_COUNT] = { 0, 1, 0, 0, 1, 1, 1, 0, 0 };

/* port: a cell changed kind (the front end redraws it) */
static void changed(uint16_t cell)
{
    if (gs.num_changed < MAX_CHANGED) gs.changed[gs.num_changed++] = cell;
    else gs.events |= EV_LOADED;            /* too many: redraw everything */
}

void game_init(void)
{
    memset(&gs, 0, sizeof(gs));
    gs.mode = GM_TITLE;
    gs.lives = 3;
    gs.level_index = 0;
    gs.air = AIR_MAX;
    tmap_init(gs.kind, GRID_W, GRID_H);
    plat_shift = 4;                         /* 16-pixel tiles */
    plat_bw = plat_bh = 16;                 /* the player is 16 x 16 */
    plat_solid = solid_kinds;
    plat_outside = T_SOLID;                 /* tile_at_pixel(): off the grid is solid */
}

static void spawn_player(void)
{
    Player *p = &gs.player;
    p->b.x = gs.spawn_x;
    p->b.y = gs.spawn_y;
    p->b.vx = 0;
    p->b.vy = 0;
    p->facing = 1;
    p->b.on_ground = 0;
    p->frame = 0;
    p->jump_lock = 1;       /* release jump before it counts */
}

/* Decode the cavern layout into (grid + player spawn + guardians). */
void game_load_level(uint8_t index)
{
    const GuardianSpec *g;
    uint8_t c, r;
    if (index >= num_levels) index = 0;

    tmap_init(gs.kind, GRID_W, GRID_H);     /* port: the solver copies gs around */
    tmap_load(lv_maps[index]);
    memset(gs.guards, 0, sizeof(gs.guards));
    gs.num_guards = 0;
    gs.num_falling = 0;
    gs.keys_total = gs.keys_remaining = (uint8_t)tmap_count(T_KEY);
    if (tmap_find(T_SPAWN, &c, &r)) {
        gs.spawn_x = c * TILE_W;
        gs.spawn_y = r * TILE_H;
        TMAP(c, r) = T_EMPTY;
    }

    for (g = level_guards[index]; g->axis != 0xFF && gs.num_guards < MAX_GUARDIANS; g++) {
        Guardian *G = &gs.guards[gs.num_guards++];
        G->axis = g->axis;
        G->colour = g->colour;
        G->dx = g->dx;
        G->dy = g->dy;
        G->x = g->x;
        G->y = g->y;
        G->min_pos = g->min_pos;
        G->max_pos = g->max_pos;
        G->alive = 1;
        G->frame = 0;
    }

    gs.air = AIR_MAX;
    gs.level_ticks = 0;
    gs.events |= EV_LOADED;
    spawn_player();
}

static void restart_current(void)
{
    game_load_level(gs.level_index);
}

/* AABB overlap between player (16x16) and any live guardian (12x12
 * hitbox - a touch smaller so grazing isn't instant death). */
static uint8_t player_touching_guardian(void)
{
    int16_t px1 = gs.player.b.x, py1 = gs.player.b.y;
    int16_t px2 = px1 + 15, py2 = py1 + 15;
    uint8_t i;
    const Guardian *G = gs.guards;
    for (i = 0; i < gs.num_guards; i++, G++) {
        if (!G->alive) continue;
        if (px1 <= G->x + 13 && px2 >= G->x + 2 && py1 <= G->y + 13 && py2 >= G->y + 2) return 1;
    }
    return 0;
}

static void kill_player(void)
{
    gs.events |= EV_DIED;
    if (gs.lives > 0) gs.lives--;
    if (gs.lives == 0) {
        gs.mode = GM_LOSE;
        gs.state_timer = 150;
        gs.events |= EV_GAMEOVER;
    } else {
        restart_current();
    }
}

uint8_t game_collapse_timer(uint16_t cell)
{
    uint8_t i;
    for (i = 0; i < gs.num_falling; i++)
        if (gs.falling[i].cell == cell) return gs.falling[i].timer;
    return 0;
}

static void update_player(uint8_t input)
{
    Player *p = &gs.player;
    uint8_t below;
    int16_t cx, cy;

    /* Input -> intended velocity. */
    p->b.vx = 0;
    if (input & INPUT_LEFT)  { p->b.vx = -WALK_SPEED; p->facing = 0; }
    if (input & INPUT_RIGHT) { p->b.vx = +WALK_SPEED; p->facing = 1; }

    /* Conveyor override - only while grounded on a conveyor tile directly
     * beneath the player's centreline. */
    if (p->b.on_ground) {
        below = plat_kind_at(p->b.x + 8, p->b.y + 16);
        if (below == T_CONV_L && p->b.vx > -WALK_SPEED) p->b.vx -= CONVEYOR_PUSH;
        if (below == T_CONV_R && p->b.vx < +WALK_SPEED) p->b.vx += CONVEYOR_PUSH;
    }

    /* Jump: on the ground and jump not held from before (edge trigger). */
    if (!(input & INPUT_JUMP)) p->jump_lock = 0;
    if ((input & INPUT_JUMP) && !p->jump_lock && p->b.on_ground) {
        p->b.vy = JUMP_IMPULSE;
        p->b.on_ground = 0;
        p->jump_lock = 1;
        gs.events |= EV_JUMP;
    }

    /* X then Y with tile collision: sdk plat.h is the original's code */
    plat_move_x(&p->b);
    plat_move_y(&p->b, GRAVITY, MAX_FALL_SPEED);

    /* Animation frame - every 6 ticks, when actually moving. */
    if (p->b.vx != 0 && gs.phase6 == 0) p->frame = (p->frame + 1) & 3;

    /* Collectibles + hazard tiles at the player's centre. */
    cx = p->b.x + 8;
    cy = p->b.y + 8;
    if (cx >= 0 && cy >= 0 && cx < PLAY_W && cy < PLAY_H) {
        uint8_t col = (uint8_t)(cx >> 4), row = (uint8_t)(cy >> 4);
        uint8_t *t = &TMAP(col, row);
        if (*t == T_KEY) {
            *t = T_EMPTY;
            changed((uint16_t)(t - gs.kind));
            gs.keys_remaining--;
            gs.score += KEY_SCORE;
            gs.events |= gs.keys_remaining ? EV_KEY : (EV_KEY | EV_UNLOCK);
        } else if (*t == T_HAZARD) {
            kill_player();
            return;
        } else if (*t == T_EXIT && gs.keys_remaining == 0) {
            /* Cavern complete. */
            gs.score += LEVEL_SCORE + (gs.air / 2);
            gs.level_index++;
            if (gs.level_index >= num_levels) {
                gs.mode = GM_WIN;
                gs.state_timer = 200;
                gs.events |= EV_WON | EV_LEVEL;
            } else {
                gs.mode = GM_LEVELWIN;
                gs.state_timer = 60;
                gs.events |= EV_LEVEL;
            }
            return;
        }
    }

    /* Start the collapse timer for a collapsing tile directly under the
     * player's feet. */
    if (p->b.on_ground) {
        int16_t bx = p->b.x + 8, by = p->b.y + 16;
        if (bx >= 0 && by >= 0 && bx < PLAY_W && by < PLAY_H) {
            uint8_t *b = &TMAP(bx >> 4, by >> 4);
            uint16_t cell = (uint16_t)(b - gs.kind);
            if (*b == T_COLLAPSE && !game_collapse_timer(cell) && gs.num_falling < MAX_FALLING) {
                gs.falling[gs.num_falling].cell = cell;
                gs.falling[gs.num_falling].timer = COLLAPSE_FRAMES;
                gs.num_falling++;
            }
        }
    }

    /* Fall-out-of-world sanity. */
    if (p->b.y >= PLAY_H) kill_player();
}

static void update_guardians(void)
{
    uint8_t i;
    Guardian *G = gs.guards;
    for (i = 0; i < gs.num_guards; i++, G++) {
        if (!G->alive) continue;
        if (G->axis == 0) {
            G->x += G->dx;
            if (G->x <= G->min_pos) { G->x = G->min_pos; G->dx = -G->dx; }
            if (G->x >= G->max_pos) { G->x = G->max_pos; G->dx = -G->dx; }
        } else {
            G->y += G->dy;
            if (G->y <= G->min_pos) { G->y = G->min_pos; G->dy = -G->dy; }
            if (G->y >= G->max_pos) { G->y = G->max_pos; G->dy = -G->dy; }
        }
        G->frame++;
    }
}

/* Collapsing tiles count down every tick once touched, then vanish. */
static void update_collapse(void)
{
    uint8_t i = 0;
    while (i < gs.num_falling) {
        Falling *f = &gs.falling[i];
        if (--f->timer == 0) {
            gs.kind[f->cell] = T_EMPTY;
            changed(f->cell);
            *f = gs.falling[--gs.num_falling];
            continue;
        }
        if ((f->timer & 1) == 0) changed(f->cell);   /* port: the crumbling picture */
        i++;
    }
}

static void drain_air(void)
{
    if (gs.phase6 == 0 && gs.air > 0) gs.air--;
    if (gs.air == 0) kill_player();
}

void game_tick(uint8_t input)
{
    gs.tick++;
    if (++gs.phase6 == AIR_DRAIN_FRAMES) gs.phase6 = 0;
    gs.events = 0;
    gs.num_changed = 0;

    if (gs.mode == GM_TITLE) {
        if (input & INPUT_START) {
            gs.level_index = 0;
            gs.lives = 3;
            gs.score = 0;
            game_load_level(0);
            gs.mode = GM_PLAYING;
        }
        return;
    }

    if (gs.mode == GM_WIN || gs.mode == GM_LOSE) {
        if (gs.state_timer > 0) gs.state_timer--;
        if ((input & INPUT_START) && gs.state_timer == 0) gs.mode = GM_TITLE;
        return;
    }

    if (gs.mode == GM_LEVELWIN) {
        if (gs.state_timer > 0) { gs.state_timer--; return; }
        game_load_level(gs.level_index);
        gs.mode = GM_PLAYING;
        return;
    }

    /* GM_PLAYING */
    gs.level_ticks++;
    update_player(input);
    if (gs.mode != GM_PLAYING) return;      /* level ended mid-tick */
    update_guardians();
    update_collapse();
    drain_air();
    if (gs.mode != GM_PLAYING) return;
    if (player_touching_guardian()) kill_player();
}
