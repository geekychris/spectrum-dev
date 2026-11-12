/*
 * Bullion Dash - Enemy AI (the original's enemy.c)
 *
 * Pathfinding toward the player, gravity, trapping in dug holes, gold
 * stealing, and respawning.
 *
 * port: the original runs a breadth-first search from each guard to the
 * player every third frame (~0.5 M T-states each in C on a Z80). The SDK's
 * gridbfs keeps one distance field from the player instead and gives the
 * same first step (see gridbfs.h); level.c keeps its cell flags in step with
 * the tiles: enterable = the original's can_enter, left/right = has_support
 * or a bar, up = a ladder, down = always. The rest is the original's.
 */
#include "enemy.h"
#include "level.h"
#include "game.h"
#include "coords.h"
#include "gridbfs.h"

/* the original's LCG (32-bit; its state carries on from game to game) */
uint32_t enemy_rng_state = 12345;      /* (not static: the test reads it) */
static int16_t enemy_rand(void)
{
    enemy_rng_state = enemy_rng_state * 1103515245UL + 12345;
    return (int16_t)((enemy_rng_state >> 16) & 0x7FFF);
}

static uint8_t is_solid(uint8_t t)
{
    return (t == TILE_BRICK || t == TILE_SOLID || t == TILE_TRAP);
}

/* can an enemy enter this tile? (not a trap, unlike the player) */
uint8_t enemy_can_enter(uint8_t t)
{
    return (t == TILE_EMPTY || t == TILE_GOLD || t == TILE_LADDER ||
            t == TILE_BAR || t == TILE_HIDDEN_LADDER);
}
#define can_enter enemy_can_enter

uint8_t enemy_has_support(int16_t gx, int16_t gy)
{
    uint8_t below;

    if (gy >= GRID_ROWS - 1)
        return 1;
    if (tile_at(gx, gy) == TILE_LADDER)
        return 1;
    below = tile_at(gx, gy + 1);
    if (is_solid(below) || below == TILE_LADDER)
        return 1;
    return 0;
}
#define has_support enemy_has_support

/* port: the original's bfs_find_next_move, from the distance field */
static uint8_t find_next_move(Enemy *e, int8_t *out_dx, int8_t *out_dy)
{
    uint8_t step;
    gb_target((uint8_t)gs.player.gx, (uint8_t)gs.player.gy);
    step = gb_step((uint8_t)e->gx, (uint8_t)e->gy);
    *out_dx = 0;
    *out_dy = 0;
    switch (step) {
    case 0:    return 1;            /* already on the player: no move */
    case GB_L: *out_dx = -1; return 1;
    case GB_R: *out_dx = 1;  return 1;
    case GB_U: *out_dy = -1; return 1;
    case GB_D: *out_dy = 1;  return 1;
    }
    return 0;                       /* no path */
}

/* respawn a dead enemy at an empty spot on the top row */
static void enemy_respawn(Enemy *e)
{
    int16_t x, attempts;

    attempts = 0;
    do {
        x = enemy_rand() % GRID_COLS;
        attempts++;
    } while (tile_at(x, 0) != TILE_EMPTY && attempts < 100);

    if (attempts >= 100) {
        for (x = 0; x < GRID_COLS; x++) {
            if (tile_at(x, 0) == TILE_EMPTY)
                break;
        }
        if (x >= GRID_COLS) x = GRID_COLS / 2;
    }

    e->gx = x;
    e->gy = 0;
    e->px = x * TILE_W;
    e->py = 0;
    e->state = ES_FALLING;
    e->dir = DIR_NONE;
    e->anim = 0;
    e->trap_timer = 0;
    /* has_gold is kept: the gold stays tracked on the respawned enemy */
    e->active = 1;
    e->move_timer = 0;
}

static void enemy_update_one(Enemy *e)
{
    int8_t move_dx, move_dy;
    int16_t new_gx, new_gy;
    uint8_t here;

    if (!e->active)
        return;

    /*--- Trapped state ---*/
    if (e->state == ES_TRAPPED) {
        e->trap_timer--;
        e->anim++;

        if (e->trap_timer <= 0) {
            /* Escape upward */
            if (e->gy > 0 && can_enter(tile_at(e->gx, e->gy - 1))) {
                e->gy--;
                e->py = e->gy * TILE_H;
                e->state = ES_RUNNING;
            } else {
                /* Can't escape - brick reformed, die */
                e->state = ES_DEAD;
                if (e->has_gold) {
                    /* drop the gold in the first empty cell above */
                    int16_t yy;
                    for (yy = e->gy - 1; yy >= 0; yy--) {
                        if (tile_at(e->gx, yy) == TILE_EMPTY) {
                            tile_set(e->gx, yy, TILE_GOLD);
                            e->has_gold = 0;
                            break;
                        }
                    }
                }
            }
        }
        return;
    }

    /*--- Dead state: respawn ---*/
    if (e->state == ES_DEAD) {
        enemy_respawn(e);
        return;
    }

    /*--- Falling state ---*/
    here = tile_at(e->gx, e->gy);

    if (e->state == ES_FALLING ||
        (!has_support(e->gx, e->gy) &&
         here != TILE_BAR &&
         here != TILE_LADDER)) {

        e->state = ES_FALLING;
        e->py += ENEMY_SPEED;

        new_gy = CELL_Y(e->py);
        if (new_gy != e->gy && ALIGNED_Y(e->py)) {
            e->gy = new_gy;

            if (e->gy >= GRID_ROWS) {
                e->state = ES_DEAD;
                return;
            }

            if (has_support(e->gx, e->gy)) {
                e->state = ES_RUNNING;
                e->py = e->gy * TILE_H;
            }
        }
        return;
    }

    /*--- fix: how long stolen gold has been carried (trap_timer counts, as
     *    it is only used while trapped; the drop is below) ---*/
    if (e->has_gold && e->trap_timer)
        e->trap_timer--;

    /*--- Movement timing (fix: frames to wait before the next step) ---*/
    if (e->move_timer) {
        e->move_timer--;
        return;
    }

    /*--- AI pathfinding ---*/
    if (!find_next_move(e, &move_dx, &move_dy)) {
        /* No path - random movement */
        switch (enemy_rand() % 4) {
        case 0: move_dx = -1; break;
        case 1: move_dx = 1;  break;
        case 2: move_dy = -1; break;
        case 3: move_dy = 1;  break;
        }
    }

    /*--- Apply movement ---*/
    here = tile_at(e->gx, e->gy);

    if (move_dx != 0) {
        new_gx = e->gx + move_dx;
        if (new_gx >= 0 && new_gx < GRID_COLS) {
            if (can_enter(tile_at(new_gx, e->gy)) &&
                (has_support(e->gx, e->gy) || here == TILE_BAR)) {
                e->gx = new_gx;
                e->px = e->gx * TILE_W;
                e->dir = (move_dx < 0) ? DIR_LEFT : DIR_RIGHT;

                if (tile_at(e->gx, e->gy) == TILE_BAR)
                    e->state = ES_HANGING;
                else
                    e->state = ES_RUNNING;
            }
        }
    } else if (move_dy != 0) {
        if (move_dy < 0) {
            /* Up - only on ladder */
            if (here == TILE_LADDER) {
                new_gy = e->gy - 1;
                if (new_gy >= 0 && can_enter(tile_at(e->gx, new_gy))) {
                    e->gy = new_gy;
                    e->py = e->gy * TILE_H;
                    e->state = ES_CLIMBING;
                    e->dir = DIR_UP;
                }
            }
        } else {
            /* Down - ladder or fall */
            if (here == TILE_LADDER ||
                tile_at(e->gx, e->gy + 1) == TILE_LADDER) {
                new_gy = e->gy + 1;
                if (new_gy < GRID_ROWS && can_enter(tile_at(e->gx, new_gy))) {
                    e->gy = new_gy;
                    e->py = e->gy * TILE_H;
                    e->state = ES_CLIMBING;
                    e->dir = DIR_DOWN;
                }
            } else if (here == TILE_BAR) {
                /* Release from bar */
                e->state = ES_FALLING;
            }
        }
    }

    e->move_timer = (move_dy != 0 ? ENEMY_CLIMB_FRAMES : ENEMY_STEP_FRAMES) - 1;
    e->anim++;

    /*--- Gold stealing ---*/
    if (!e->has_gold && tile_at(e->gx, e->gy) == TILE_GOLD) {
        /* 30% chance to pick up gold */
        if ((enemy_rand() % 100) < 30) {
            e->has_gold = 1;
            e->trap_timer = ENEMY_CARRY_FRAMES;     /* fix: carried this long */
            tile_set(e->gx, e->gy, TILE_EMPTY);
        }
    /* fix: after ENEMY_CARRY_FRAMES the gold is dropped on the first empty
       cell the guard steps onto where gold lay at the start of the level */
    } else if (e->has_gold && !e->trap_timer && tile_at(e->gx, e->gy) == TILE_EMPTY
               && level_gold_home(e->gx, e->gy)) {
        tile_set(e->gx, e->gy, TILE_GOLD);
        e->has_gold = 0;
    }

    /*--- Drop gold when falling into hole ---*/
    if (e->has_gold && e->state == ES_TRAPPED) {
        if (e->gy > 0 && tile_at(e->gx, e->gy - 1) == TILE_EMPTY) {
            tile_set(e->gx, e->gy - 1, TILE_GOLD);
            e->has_gold = 0;
        }
    }
}

void enemy_update_all(void)
{
    int16_t i;
    for (i = 0; i < gs.num_enemies; i++)
        enemy_update_one(&gs.enemies[i]);
}

/* 1 if an enemy overlaps the player (pixel boxes) */
uint8_t enemy_check_collision(void)
{
    int16_t i, dx_abs, dy_abs;
    Player *p = &gs.player;
    Enemy *e = gs.enemies;

    for (i = 0; i < gs.num_enemies; i++, e++) {
        if (!e->active)
            continue;
        if (e->state == ES_TRAPPED || e->state == ES_DEAD)
            continue;

        dx_abs = e->px - p->px;
        if (dx_abs < 0) dx_abs = -dx_abs;
        dy_abs = e->py - p->py;
        if (dy_abs < 0) dy_abs = -dy_abs;

        if (dx_abs < (TILE_W - 2) && dy_abs < (TILE_H - 2))
            return 1;
    }
    return 0;
}

/* a brick was dug at (x, y): an enemy standing there is trapped */
void enemy_trap_check(int16_t x, int16_t y)
{
    int16_t i;
    Enemy *e = gs.enemies;
    for (i = 0; i < gs.num_enemies; i++, e++) {
        if (!e->active)
            continue;
        if (e->state == ES_TRAPPED || e->state == ES_DEAD)
            continue;

        if (e->gx == x && e->gy == y) {
            e->state = ES_TRAPPED;
            e->trap_timer = BRICK_REGEN_FRAMES - 10;
            e->px = x * TILE_W;
            e->py = y * TILE_H;
            sfx_pending = SFX_TRAP;

            /* Drop gold above the hole (only if the cell is free) */
            if (e->has_gold) {
                if (y > 0 && tile_at(x, y - 1) == TILE_EMPTY) {
                    tile_set(x, y - 1, TILE_GOLD);
                    e->has_gold = 0;
                }
            }
        }
    }
}
