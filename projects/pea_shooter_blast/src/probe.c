/*
 * probe.c - what test.py reads (by symbol): the game's state in fixed
 * places, updated once a loop. In page 84 (memmap.h) with the HUD.
 */
#include <stdint.h>
#include "psb.h"
#include "view.h"

extern uint16_t frame_overruns;

typedef struct { int16_t x, y; uint8_t type, active, health, state; } ProbeEnemy;
struct {
    uint8_t mode, state, level, lives, health, weapon, alive, on_ground, facing, fps;
    int16_t tx, ty, scroll;
    int32_t score;
    uint16_t invuln, overruns, cols;
    ProbeEnemy en[MAX_ENEMIES];
    int16_t ebx[MAX_ENEMY_BULLETS], eby[MAX_ENEMY_BULLETS];
    uint8_t ebon[MAX_ENEMY_BULLETS];
    int16_t pux[MAX_POWERUPS], puy[MAX_POWERUPS];
    uint8_t puon[MAX_POWERUPS];
} probe;

void update_probe(const GameState *gsp, uint8_t mode, uint8_t fps)
{
    uint8_t i;
    probe.mode = mode; probe.state = (uint8_t)gsp->state; probe.level = (uint8_t)gsp->level;
    probe.lives = (uint8_t)gsp->lives; probe.health = (uint8_t)gsp->tank.health;
    probe.weapon = (uint8_t)gsp->tank.weapon_level; probe.alive = (uint8_t)gsp->tank.alive;
    probe.on_ground = (uint8_t)gsp->tank.on_ground; probe.facing = (uint8_t)gsp->tank.facing; probe.fps = fps;
    probe.tx = (int16_t)FIX_INT(gsp->tank.x); probe.ty = (int16_t)FIX_INT(gsp->tank.y);
    probe.scroll = (int16_t)gsp->scroll.pixel_x; probe.score = gsp->score;
    probe.invuln = (uint16_t)gsp->tank.invuln_timer; probe.overruns = frame_overruns; probe.cols = view_cols;
    for (i = 0; i < MAX_ENEMIES; i++) {
        ProbeEnemy *p = &probe.en[i];
        const Enemy *e = &gsp->enemies[i];
        p->x = (int16_t)FIX_INT(e->x); p->y = (int16_t)FIX_INT(e->y);
        p->type = (uint8_t)e->type; p->active = (uint8_t)e->active; p->health = (uint8_t)e->health;
        p->state = (uint8_t)e->state;
    }
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        probe.ebx[i] = (int16_t)FIX_INT(gsp->enemy_bullets[i].x);
        probe.eby[i] = (int16_t)FIX_INT(gsp->enemy_bullets[i].y);
        probe.ebon[i] = (uint8_t)gsp->enemy_bullets[i].active;
    }
    for (i = 0; i < MAX_POWERUPS; i++) {
        probe.pux[i] = (int16_t)FIX_INT(gsp->powerups[i].x);
        probe.puy[i] = (int16_t)FIX_INT(gsp->powerups[i].y);
        probe.puon[i] = (uint8_t)gsp->powerups[i].active;
    }
}

