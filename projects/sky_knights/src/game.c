/*
 * SKY KNIGHTS - Game logic (port of amiga_mcp/examples/sky_knights/game.c)
 * Physics, collision, enemy AI, wave management
 * (Original aerial-combat mechanics; not affiliated with any prior arcade title.)
 *
 * port: the same rules; the changes are for the Z80 and marked "port:" -
 * 16-bit fixed point (game.h), pointer walks instead of indexing arrays of
 * structs (zsdcc multiplies for those), the SDK's xorshift random numbers.
 */
#include <string.h>
#include "game.h"

/* Platform layout */
static const Platform default_platforms[] = {
    { 0,   GROUND_Y, 320 },   /* ground - full width */
    { 20,  180, 80  },         /* lower left */
    { 220, 180, 80  },         /* lower right */
    { 110, 140, 100 },         /* middle center */
    { 0,   100, 70  },         /* upper left */
    { 250, 100, 70  },         /* upper right */
    { 130,  60, 60  },         /* top center */
};
#define NUM_DEFAULT_PLATFORMS 7

/* port: the original's 32-bit LCG costs a 32-bit multiply per call and the
   swoopers call it every frame; the SDK's xorshift gives the same 0..0x7FFF
   range (seeded with the original's 12345 at boot, main.c) */
static WORD rng(void)
{
    return (WORD)(rnd16() >> 1);
}

/* port: gravity 0.3 px/frame^2 is 19.2/64: 19, and 20 every fifth frame */
Fixed gravity;
static uint8_t gravity_phase;

/* port: the body physics (gravity, move, wrap, ceiling, ledges, floor) and
   the ledge search are in phys.asm; the C they replace is kept below under
   PHYS_IN_C (build with EXTRA_CFLAGS=-DPHYS_IN_C=1 to use it) */
#ifndef PHYS_IN_C
#define PHYS_IN_C 0
#endif
extern Platform *phys_plats;
extern uint8_t phys_nplats, phys_above, ps_w, ps_first;
extern WORD ps_px, ps_py;
extern WORD plat_scan(void);
extern uint8_t body_step(void *body) __z88dk_fastcall;

/* logic.asm: the per-frame loops; they call back into the C below for what
   happens rarely */
extern Player *lg_players, *jp_player;
extern Enemy *lg_enemies, *jp_enemy;
extern Egg *lg_eggs;
extern uint8_t lg_nplayers, lg_frame, jp_index;
extern void enemies_update(void);
extern void eggs_update(void);
extern void combat_scan(void);
static GameState *cur;          /* the game the callbacks work on */

void game_init(GameState *gs)
{
    WORD i;
    memset(gs, 0, sizeof(GameState));

    gs->state = STATE_TITLE;
    gs->wave = 0;
    gs->num_players = 1;
    gs->title_selection = 0;

    /* Setup platforms */
    gs->num_platforms = NUM_DEFAULT_PLATFORMS;
    memcpy(gs->platforms, default_platforms, sizeof(default_platforms));
    phys_plats = gs->platforms;
    phys_nplats = (uint8_t)gs->num_platforms;

    /* Init players */
    for (i = 0; i < MAX_PLAYERS; i++) {
        gs->players[i].lives = 3;
        gs->players[i].active = 0;
        gs->players[i].alive = 0;
        gs->score[i] = 0;
    }
}

static void spawn_player(GameState *gs, WORD idx)
{
    Player *p = &gs->players[idx];
    p->x = (idx == 0) ? FIX(80) : FIX(220);
    p->y = FIX(GROUND_Y - PLAYER_H);
    p->dx = 0;
    p->dy = 0;
    p->alive = 1;
    p->active = 1;
    p->facing = (idx == 0) ? 0 : 1;
    p->anim_frame = 0;
    p->anim_timer = 0;
    p->on_ground = 1;
    p->respawn_timer = 0;
    p->flapping = 0;
}

void game_start(GameState *gs)
{
    WORD i;

    gs->wave = 0;
    gs->state = STATE_WAVE_INTRO;
    gs->state_timer = 100;

    for (i = 0; i < MAX_ENEMIES; i++)
        gs->enemies[i].active = 0;
    for (i = 0; i < MAX_EGGS; i++)
        gs->eggs[i].active = 0;

    /* Reset players */
    for (i = 0; i < gs->num_players; i++) {
        gs->players[i].lives = 3;
        gs->score[i] = 0;
        spawn_player(gs, i);
    }

    gs->wave = 1;
    gs->ev_flags |= EV_WAVE;
}

static void spawn_enemy(GameState *gs, WORD type)
{
    WORD i;
    Enemy *e = gs->enemies;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        if (!e->active) {
            e->active = 1;
            e->type = type;
            e->facing = rng() & 1;

            /* Spawn at random platform */
            {
                WORD pi = 1 + (rng() % (gs->num_platforms - 1)); /* not ground */
                Platform *pl = &gs->platforms[pi];
                e->x = FIX(pl->x + (rng() % pl->w));
                e->y = FIX(pl->y - PLAYER_H);
            }

            e->dx = e->facing ? -ENEMY_SLOW : ENEMY_SLOW;
            e->dy = 0;
            e->anim_frame = 0;
            e->anim_timer = 0;
            e->on_ground = 0;
            e->flap_timer = 20 + (rng() % 30);

            /* Adjust speed by type */
            if (type == ETYPE_RAIDER) {
                e->dx = e->facing ? -ENEMY_MED : ENEMY_MED;
            } else if (type == ETYPE_WRAITH) {
                e->dx = e->facing ? -ENEMY_FAST : ENEMY_FAST;
            }
            return;
        }
    }
}

void game_start_wave(GameState *gs)
{
    WORD i;
    WORD num_enemies;
    WORD swoopers, raiders, wraiths;

    /* Clear old enemies and pods */
    for (i = 0; i < MAX_ENEMIES; i++)
        gs->enemies[i].active = 0;
    for (i = 0; i < MAX_EGGS; i++)
        gs->eggs[i].active = 0;

    /* Calculate enemy counts based on wave */
    num_enemies = 2 + gs->wave;
    if (num_enemies > MAX_ENEMIES) num_enemies = MAX_ENEMIES;

    swoopers = 0;
    raiders = 0;
    wraiths = 0;

    if (gs->wave <= 2) {
        swoopers = num_enemies;
    } else if (gs->wave <= 4) {
        swoopers = num_enemies / 2;
        raiders = num_enemies - swoopers;
    } else {
        swoopers = num_enemies / 3;
        raiders = num_enemies / 3;
        wraiths = num_enemies - swoopers - raiders;
        if (wraiths < 1) wraiths = 1;
    }

    for (i = 0; i < swoopers; i++) spawn_enemy(gs, ETYPE_SWOOPER);
    for (i = 0; i < raiders; i++)  spawn_enemy(gs, ETYPE_RAIDER);
    for (i = 0; i < wraiths; i++)  spawn_enemy(gs, ETYPE_WRAITH);
}

/* Check if entity is standing on a platform */
static WORD check_platform(GameState *gs, Fixed fx, Fixed fy, WORD w, WORD h)
{
#if PHYS_IN_C
    WORD px = FIX_INT(fx);
    WORD py = FIX_INT(fy) + h;
    WORD i;
    Platform *pl = gs->platforms;

    for (i = gs->num_platforms; i; i--, pl++) {
        if (py >= pl->y && py <= pl->y + PLAT_THICK) {
            if (px + w > pl->x && px < pl->x + pl->w) {
                return pl->y;
            }
        }
    }
    return -1;
#else
    (void)gs;
    ps_px = FIX_INT(fx);
    ps_py = FIX_INT(fy) + h;
    ps_w = (uint8_t)w;
    ps_first = 0;
    return plat_scan();
#endif
}

#if PHYS_IN_C
/* Check if entity hits underside of a platform (when rising) */
static WORD check_platform_above(GameState *gs, Fixed fx, Fixed fy, WORD w)
{
    WORD px = FIX_INT(fx);
    WORD py = FIX_INT(fy); /* top of entity */
    WORD i;
    Platform *pl = gs->platforms + 1;

    for (i = gs->num_platforms - 1; i > 0; i--, pl++) { /* skip ground */
        if (py >= pl->y && py <= pl->y + PLAT_THICK) {
            if (px + w > pl->x && px < pl->x + pl->w) {
                return pl->y + PLAT_THICK; /* bottom of platform */
            }
        }
    }
    return -1;
}

/* Horizontal wrap */
static Fixed wrap_x(Fixed fx)
{
    WORD px = FIX_INT(fx);
    if (px < -PLAYER_W) return FIX(SCREEN_W);
    if (px > SCREEN_W) return FIX(-PLAYER_W + 1);
    return fx;
}
#endif

static void update_player(GameState *gs, Player *p, UWORD input)
{
    WORD plat_y;

    if (!p->alive) {
        if (p->respawn_timer > 0) {
            p->respawn_timer--;
            if (p->respawn_timer == 0 && p->lives > 0) {
                WORD idx = (p == gs->players) ? 0 : 1;
                spawn_player(gs, idx);
            }
        }
        return;
    }

    /* Horizontal input */
    if (input & INP_LEFT) {
        p->dx = -MOVE_SPEED;
        p->facing = 1;
    } else if (input & INP_RIGHT) {
        p->dx = MOVE_SPEED;
        p->facing = 0;
    } else {
        /* Friction */
        if (p->dx > 0) {
            p->dx -= FRICTION;
            if (p->dx < 0) p->dx = 0;
        } else if (p->dx < 0) {
            p->dx += FRICTION;
            if (p->dx > 0) p->dx = 0;
        }
    }

    /* Flap */
    if (input & INP_FLAP) {
        p->dy += FLAP_IMPULSE / 4; /* spread over frames for smoother feel */
        if (p->dy < MAX_RISE_VEL) p->dy = MAX_RISE_VEL;
        p->flapping = 1;
        p->on_ground = 0;
        gs->ev_flags |= EV_FLAP;
    } else {
        p->flapping = 0;
    }

#if PHYS_IN_C
    /* Gravity */
    p->dy += gravity;
    if (p->dy > MAX_FALL_VEL) p->dy = MAX_FALL_VEL;

    /* Move */
    p->x += p->dx;
    p->y += p->dy;

    /* Horizontal wrap */
    p->x = wrap_x(p->x);

    /* Ceiling */
    if (FIX_INT(p->y) < 10) {
        p->y = FIX(10);
        p->dy = 0;
    }

    /* Platform collision from below (when rising) */
    if (p->dy < 0) {
        WORD plat_bot = check_platform_above(gs, p->x, p->y, PLAYER_W);
        if (plat_bot >= 0) {
            p->y = FIX(plat_bot);
            p->dy = 0;
        }
    }

    /* Platform collision from above (when falling) */
    plat_y = check_platform(gs, p->x, p->y, PLAYER_W, PLAYER_H);
    if (plat_y >= 0 && p->dy >= 0) {
        p->y = FIX(plat_y - PLAYER_H);
        p->dy = 0;
        p->on_ground = 1;
    } else {
        p->on_ground = 0;
    }

    /* Floor */
    if (FIX_INT(p->y) + PLAYER_H >= SCREEN_H) {
        p->y = FIX(SCREEN_H - PLAYER_H);
        p->dy = 0;
        p->on_ground = 1;
    }

#else
    phys_above = 1;
    p->on_ground = body_step(p);
#endif

    /* Animation */
    p->anim_timer++;
    if (p->anim_timer > 6) {
        p->anim_timer = 0;
        p->anim_frame ^= 1;
    }
}

/* AI: flap (port: a function, logic.asm calls it when the timer runs out) */
void enemy_flap(Enemy *e) __z88dk_fastcall
{
    {
        e->dy += FLAP_IMPULSE / 3;
        if (e->dy < MAX_RISE_VEL) e->dy = MAX_RISE_VEL;
        /* Vary flap timing by type */
        if (e->type == ETYPE_SWOOPER)
            e->flap_timer = 30 + (rng() % 40);
        else if (e->type == ETYPE_RAIDER)
            e->flap_timer = 20 + (rng() % 30);
        else
            e->flap_timer = 15 + (rng() % 20);
    }
}

/* raiders/wraiths seek players (port: a function, see enemy_flap) */
void enemy_seek(Enemy *e) __z88dk_fastcall
{
    GameState *gs = cur;
    {
        /* Find nearest active player */
        WORD i;
        Fixed best_dist = 0x7FFF;   /* port: 16-bit */
        WORD best_dir = e->facing;
        Player *p = gs->players;
        for (i = 0; i < gs->num_players; i++, p++) {
            if (p->alive) {
                Fixed dist = p->x - e->x;
                if (dist < 0) dist = -dist;
                if (dist < best_dist) {
                    best_dist = dist;
                    best_dir = (p->x < e->x) ? 1 : 0;
                }
            }
        }
        e->facing = best_dir;
        {
            Fixed spd = ENEMY_SLOW;
            if (e->type == ETYPE_RAIDER) spd = ENEMY_MED;
            else if (e->type == ETYPE_WRAITH) spd = ENEMY_FAST;
            e->dx = e->facing ? -spd : spd;
        }
    }
}

#if PHYS_IN_C
static void update_enemy(GameState *gs, Enemy *e)
{
    WORD plat_y;

    if (!e->active) return;

    /* AI: flap periodically */
    e->flap_timer--;
    if (e->flap_timer <= 0) enemy_flap(e);

    /* Gravity */
    e->dy += gravity;
    if (e->dy > MAX_FALL_VEL) e->dy = MAX_FALL_VEL;

    /* Move */
    e->x += e->dx;
    e->y += e->dy;

    /* Horizontal wrap */
    e->x = wrap_x(e->x);

    /* Ceiling */
    if (FIX_INT(e->y) < 10) {
        e->y = FIX(10);
        e->dy = 0;
    }

    /* Platform collision */
    plat_y = check_platform(gs, e->x, e->y, PLAYER_W, PLAYER_H);
    if (plat_y >= 0 && e->dy >= 0) {
        e->y = FIX(plat_y - PLAYER_H);
        e->dy = 0;
        e->on_ground = 1;
    } else {
        e->on_ground = 0;
    }

    /* Floor */
    if (FIX_INT(e->y) + PLAYER_H >= SCREEN_H) {
        e->y = FIX(SCREEN_H - PLAYER_H);
        e->dy = 0;
        e->on_ground = 1;
    }

    /* Occasionally change direction (raiders/wraiths seek players) */
    if (e->type >= ETYPE_RAIDER && (gs->frame & 63) == 0) enemy_seek(e);

    /* Random direction change for swoopers */
    if (e->type == ETYPE_SWOOPER && (rng() & 127) == 0) {
        e->facing ^= 1;
        e->dx = e->facing ? -ENEMY_SLOW : ENEMY_SLOW;
    }

    /* Animation */
    e->anim_timer++;
    if (e->anim_timer > 8) {
        e->anim_timer = 0;
        e->anim_frame ^= 1;
    }
}
#endif

static void spawn_egg(GameState *gs, Fixed x, Fixed y, WORD hatch_type)
{
    WORD i;
    Egg *eg = gs->eggs;
    for (i = 0; i < MAX_EGGS; i++, eg++) {
        if (!eg->active) {
            eg->active = 1;
            eg->x = x;
            eg->y = y;
            eg->dy = 0;
            eg->timer = EGG_HATCH_TIME;
            eg->on_ground = 0;
            eg->hatch_type = hatch_type;
            return;
        }
    }
}

/* Hatch into a stronger enemy (port: a function, logic.asm calls it) */
void egg_hatch(Egg *eg) __z88dk_fastcall
{
    WORD new_type = eg->hatch_type;
    if (new_type < ETYPE_WRAITH) new_type++;
    spawn_enemy(cur, new_type);
    eg->active = 0;
}

/* player jp_index caught the pod (port: a function, logic.asm calls it) */
void egg_pickup(Egg *eg) __z88dk_fastcall
{
    cur->score[jp_index] += SCORE_EGG;
    eg->active = 0;
    cur->ev_flags |= EV_EGG;
}

#if PHYS_IN_C
static void update_eggs(GameState *gs)
{
    WORD i;
    Egg *eg = gs->eggs;
    for (i = 0; i < MAX_EGGS; i++, eg++) {
        if (!eg->active) continue;

        /* Gravity */
        if (!eg->on_ground) {
            eg->dy += gravity;
            if (eg->dy > MAX_FALL_VEL) eg->dy = MAX_FALL_VEL;
            eg->y += eg->dy;

            /* Platform check */
            {
                WORD plat_y = check_platform(gs, eg->x, eg->y, 8, 8);
                if (plat_y >= 0 && eg->dy >= 0) {
                    eg->y = FIX(plat_y - 8);
                    eg->dy = 0;
                    eg->on_ground = 1;
                }
            }

            /* Floor */
            if (FIX_INT(eg->y) + 8 >= SCREEN_H) {
                eg->y = FIX(SCREEN_H - 8);
                eg->dy = 0;
                eg->on_ground = 1;
            }
        }

        /* Hatch timer */
        eg->timer--;
        if (eg->timer <= 0) {
            egg_hatch(eg);
            continue;   /* skip pickup check on this now-hatched pod */
        }

        /* Player pickup collision */
        {
            WORD j;
            WORD ex = FIX_INT(eg->x);
            WORD ey = FIX_INT(eg->y);
            Player *p = gs->players;
            for (j = 0; j < gs->num_players; j++, p++) {
                if (!p->alive) continue;
                {
                    WORD px = FIX_INT(p->x);
                    WORD py = FIX_INT(p->y);
                    if (px + PLAYER_W > ex && px < ex + 8 &&
                        py + PLAYER_H > ey && py < ey + 8) {
                        jp_index = (uint8_t)j;
                        egg_pickup(eg);
                        break;  /* only one player collects this pod */
                    }
                }
            }
        }
    }
}
#endif

/* A joust: player jp_player (number jp_index) and enemy jp_enemy overlap.
   Returns 1 if the player went down (no more jousts for them this frame).
   port: a function, logic.asm's scan calls it */
uint8_t joust(void)
{
    GameState *gs = cur;
    Player *p = jp_player;
    Enemy *e = jp_enemy;
    WORD i = jp_index;
    WORD px = FIX_INT(p->x);
    WORD py = FIX_INT(p->y);
    WORD ex = FIX_INT(e->x);
    WORD ey = FIX_INT(e->y);

    /* Whoever is higher wins */
    if (py < ey - 2) {
        /* Player wins */
        WORD pts = SCORE_SWOOPER;
        if (e->type == ETYPE_RAIDER) pts = SCORE_RAIDER;
        else if (e->type == ETYPE_WRAITH) pts = SCORE_WRAITH;
        gs->score[i] += pts;

        spawn_egg(gs, e->x, e->y, e->type);
        e->active = 0;
        gs->ev_flags |= EV_KILL;

        /* Bounce player up */
        p->dy = FLAP_IMPULSE / 2;
    }
    else if (ey < py - 2) {
        /* Enemy wins - player dies */
        p->alive = 0;
        p->lives--;
        p->respawn_timer = 100; /* 2 seconds */
        gs->ev_flags |= EV_DIE;

        if (p->lives <= 0) {
            p->active = 0;
            /* Check if all players dead */
            {
                WORD all_dead = 1;
                WORD k;
                for (k = 0; k < gs->num_players; k++) {
                    if (gs->players[k].lives > 0 || gs->players[k].alive) {
                        all_dead = 0;
                        break;
                    }
                }
                if (all_dead) {
                    gs->state = STATE_GAMEOVER;
                    gs->state_timer = 200;
                }
            }
        }
        /* Player is dead this frame; stop checking further enemies
         * so overlapping enemies can't decrement extra lives. */
        return 1;
    }
    else {
        /* Equal height - bounce off */
        p->dx = (px < ex) ? -MOVE_SPEED : MOVE_SPEED;
        e->dx = (ex < px) ? -ENEMY_SLOW : ENEMY_SLOW;
        p->dy = FLAP_IMPULSE / 3;
        e->dy = FLAP_IMPULSE / 3;
    }
    return 0;
}

#if PHYS_IN_C
/* Check combat between player and enemy */
static void check_combat(GameState *gs)
{
    WORD i, j;
    Player *p = gs->players;

    for (i = 0; i < gs->num_players; i++, p++) {
        Enemy *e = gs->enemies;
        if (!p->alive) continue;

        for (j = 0; j < MAX_ENEMIES; j++, e++) {
            if (!e->active) continue;

            {
                WORD px = FIX_INT(p->x);
                WORD py = FIX_INT(p->y);
                WORD ex = FIX_INT(e->x);
                WORD ey = FIX_INT(e->y);

                /* Bounding box overlap */
                if (px + PLAYER_W > ex && px < ex + PLAYER_W &&
                    py + PLAYER_H > ey && py < ey + PLAYER_H) {
                    jp_player = p;
                    jp_enemy = e;
                    jp_index = (uint8_t)i;
                    if (joust()) break;
                }
            }
        }
    }
}
#endif

void game_update(GameState *gs, InputState *input)
{
    WORD i;

    cur = gs;
    gs->frame++;
    if (++gravity_phase == 5) gravity_phase = 0;
    gravity = gravity_phase ? GRAVITY_LO : GRAVITY_LO + 1;

    switch (gs->state) {
    case STATE_TITLE:
        /* Handle selection */
        if (input->sys & INP_START1) {
            gs->num_players = 1;
            game_start(gs);
        } else if (input->sys & INP_START2) {
            gs->num_players = 2;
            game_start(gs);
        }
        /* Animate title selection */
        if (input->sys & INP_SELECT) {   /* port: was p1 & INP_FLAP; FIRE starts here */
            gs->title_selection ^= 1;
        }
        break;

    case STATE_WAVE_INTRO:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            game_start_wave(gs);
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_PLAYING:
        /* Update players */
        update_player(gs, &gs->players[0], input->p1);
        if (gs->num_players > 1) {
            update_player(gs, &gs->players[1], input->p2);
        }

#if PHYS_IN_C
        /* Update enemies */
        {
            Enemy *e = gs->enemies;
            for (i = 0; i < MAX_ENEMIES; i++, e++) {
                update_enemy(gs, e);
            }
        }

        /* Update eggs */
        update_eggs(gs);

        /* Combat */
        check_combat(gs);
#else
        lg_players = gs->players;
        lg_enemies = gs->enemies;
        lg_eggs = gs->eggs;
        lg_nplayers = (uint8_t)gs->num_players;
        lg_frame = (uint8_t)gs->frame;
        enemies_update();
        eggs_update();
        combat_scan();
#endif

        /* Check wave complete */
        {
            WORD alive = 0;
            for (i = 0; i < MAX_ENEMIES; i++) {
                if (gs->enemies[i].active) alive++;
            }
            /* Also count eggs as "alive" enemies (they can hatch) */
            for (i = 0; i < MAX_EGGS; i++) {
                if (gs->eggs[i].active) alive++;
            }
            gs->enemies_alive = alive;

            if (alive == 0) {
                /* Wave complete */
                for (i = 0; i < gs->num_players; i++) {
                    if (gs->players[i].alive) {
                        gs->score[i] += SCORE_WAVE;
                    }
                }
                gs->wave++;
                gs->state = STATE_WAVE_INTRO;
                gs->state_timer = 100;
                gs->ev_flags |= EV_WAVE;
            }
        }
        break;

    case STATE_GAMEOVER:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            game_init(gs);
        }
        break;
    }
}
