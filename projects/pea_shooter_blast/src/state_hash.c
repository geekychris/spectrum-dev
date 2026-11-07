/* A scripted game's input and a hash of the game's state (state_hash.h). */
#include "state_hash.h"
#include "port_fixes.h"

uint8_t script_input(uint16_t f)
{
    uint8_t in = (f % 200) < 170 ? 2 : 1;           /* right, now and then left */
    if ((f % 37) < 6) in |= 4;                      /* jump */
    if ((f % 300) > 280) in |= 8;                   /* down (ladders) */
    if ((f % 3) != 0) in |= 16;                     /* fire */
    return in;
}

static uint32_t h;
static void mix(int32_t v) { h ^= (uint32_t)v; h *= 16777619UL; }

uint32_t state_hash(const GameState *gs)
{
    uint8_t i;
    const Tank *t = &gs->tank;
    h = 2166136261UL;
    mix(t->x); mix(t->y); mix(t->dx); mix(t->dy); mix(t->on_ground); mix(t->facing); mix(t->alive);
    mix(t->health); mix(t->weapon_level); mix(t->gun_timer); mix(t->invuln_timer); mix(t->jump_held);
    mix(t->anim_frame);
    for (i = 0; i < MAX_BULLETS; i++) {
        const Bullet *b = &gs->bullets[i];
        mix(b->x); mix(b->y); mix(b->dx); mix(b->life); mix(b->active); mix(b->power); mix(b->size);
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &gs->enemies[i];
        mix(e->x); mix(e->y); mix(e->dx); mix(e->dy); mix(e->type); mix(e->health); mix(e->active);
        mix(e->facing); mix(e->state); mix(e->timer); mix(e->fire_timer);
    }
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        const EnemyBullet *b = &gs->enemy_bullets[i];
        mix(b->x); mix(b->y); mix(b->dx); mix(b->life); mix(b->active);
    }
    for (i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &gs->particles[i];
        mix(p->x); mix(p->y); mix(p->dx); mix(p->dy); mix(p->life); mix(p->color); mix(p->active);
    }
    for (i = 0; i < MAX_POWERUPS; i++) {
        const Powerup *p = &gs->powerups[i];
        mix(p->x); mix(p->y); mix(p->type); mix(p->active);
    }
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        const Explosion *e = &gs->explosions[i];
        mix(e->radius); mix(e->life); mix(e->active);
    }
    mix(gs->scroll.pixel_x); mix(gs->score); mix(gs->lives); mix(gs->level); mix(gs->state);
    mix(gs->state_timer); mix(gs->frame);
    return h;
}

uint32_t hash_game(GameState *gs, uint16_t *frames)
{
    uint16_t f;
    game_init(gs);
    port_fixes(gs);
    for (f = 0; f < SCRIPT_FRAMES && gs->state != STATE_TITLE; f++) {
        uint8_t in = script_input(f);
        game_update(gs, in & 1, (in >> 1) & 1, (in >> 2) & 1, (in >> 3) & 1, (in >> 4) & 1);
    }
    *frames = f;
    return state_hash(gs);
}
