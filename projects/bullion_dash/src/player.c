/*
 * Bullion Dash - Player movement and actions (the original's player.c)
 *
 * Handles input-driven movement, gravity, digging, gold collection,
 * and smooth pixel-level movement between grid positions.
 *
 * port: the game state is the global `gs` (zsdcc makes slow code through a
 * struct pointer), tiles change through tile_set(), the grid-alignment tests
 * use a counter-free remainder, and picking up gold / digging asks for a
 * sound. The rules are the original's, line for line.
 */
#include "player.h"
#include "level.h"
#include "enemy.h"
#include "game.h"

static uint8_t is_solid(uint8_t t)
{
    return (t == TILE_BRICK || t == TILE_SOLID || t == TILE_TRAP);
}

/* support: on a ladder, or solid/ladder below, or on the bottom row */
static uint8_t has_support(int16_t gx, int16_t gy)
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

#define is_on_bar(gx, gy)    (tile_at(gx, gy) == TILE_BAR)
#define is_on_ladder(gx, gy) (tile_at(gx, gy) == TILE_LADDER)

/* empty, gold, ladder, bar, hidden ladder or trap; not solid or brick */
static uint8_t can_enter(uint8_t t)
{
    return (t == TILE_EMPTY || t == TILE_GOLD || t == TILE_LADDER ||
            t == TILE_BAR || t == TILE_HIDDEN_LADDER || t == TILE_TRAP);
}

#define grid_to_pixel_x(gx) ((gx) * TILE_W)
#define grid_to_pixel_y(gy) ((gy) * TILE_H)
#define is_aligned_x(px) (((px) % TILE_W) == 0)
#define is_aligned_y(py) (((py) % TILE_H) == 0)

static int16_t move_toward(int16_t current, int16_t target, int16_t speed)
{
    if (current < target) {
        current += speed;
        if (current > target) current = target;
    } else if (current > target) {
        current -= speed;
        if (current < target) current = target;
    }
    return current;
}

static void pick_up_gold(void)
{
    Player *p = &gs.player;
    if (tile_at(p->gx, p->gy) == TILE_GOLD) {
        tile_set(p->gx, p->gy, TILE_EMPTY);
        gs.gold_collected++;
        gs.score += 100;
        sfx_pending = SFX_GOLD;
    }
}

/*--------------------------------------------------------------------
 * player_update - Process one frame of player movement
 *
 * dx: -1 = left, 0 = none, 1 = right
 * dy: -1 = up, 0 = none, 1 = down
 * dig_left / dig_right: 1 = dig button pressed
 *-------------------------------------------------------------------*/
void player_update(int8_t dx, int8_t dy, uint8_t dig_left, uint8_t dig_right)
{
    Player *p = &gs.player;
    int16_t target_px, target_py;
    int16_t new_gx, new_gy;

    /* Dead players don't move */
    if (p->state == PS_DEAD)
        return;

    /* Digging animation in progress */
    if (p->state == PS_DIGGING) {
        p->dig_timer--;
        if (p->dig_timer <= 0) {
            p->state = PS_IDLE;
            p->dig_timer = 0;
        }
        p->anim++;
        return;
    }

    /*--- Gravity check ---*/
    if (!is_on_ladder(p->gx, p->gy) &&
        !is_on_bar(p->gx, p->gy) &&
        !has_support(p->gx, p->gy)) {

        p->state = PS_FALLING;
        p->py += PLAYER_SPEED;
        p->fall_count++;

        new_gy = p->py / TILE_H;
        if (new_gy != p->gy && is_aligned_y(p->py)) {
            p->gy = new_gy;

            /* Fell off bottom of screen = death */
            if (p->gy >= GRID_ROWS) {
                p->state = PS_DEAD;
                return;
            }

            /* Check if we landed */
            if (has_support(p->gx, p->gy)) {
                p->state = PS_IDLE;
                p->fall_count = 0;
                p->py = grid_to_pixel_y(p->gy);
            }
        }
        /* Pick up gold while falling through */
        pick_up_gold();
        return;
    }

    p->fall_count = 0;

    /*--- Process digging ---*/
    if (dig_left && is_aligned_x(p->px) && is_aligned_y(p->py)) {
        int16_t dig_x = p->gx - 1;
        int16_t dig_y = p->gy + 1;
        /* Can dig left: brick below-left, and empty to the left */
        if (dig_x >= 0 && dig_y < GRID_ROWS &&
            tile_at(dig_x, dig_y) == TILE_BRICK &&
            can_enter(tile_at(dig_x, p->gy))) {
            if (level_dig_brick(dig_x, dig_y)) {
                p->state = PS_DIGGING;
                p->dig_timer = 10;
                p->dir = DIR_LEFT;
                sfx_pending = SFX_DIG;
                /* an enemy already standing in the fresh hole is trapped */
                enemy_trap_check(dig_x, dig_y);
                return;
            }
        }
    }

    if (dig_right && is_aligned_x(p->px) && is_aligned_y(p->py)) {
        int16_t dig_x = p->gx + 1;
        int16_t dig_y = p->gy + 1;
        if (dig_x < GRID_COLS && dig_y < GRID_ROWS &&
            tile_at(dig_x, dig_y) == TILE_BRICK &&
            can_enter(tile_at(dig_x, p->gy))) {
            if (level_dig_brick(dig_x, dig_y)) {
                p->state = PS_DIGGING;
                p->dig_timer = 10;
                p->dir = DIR_RIGHT;
                sfx_pending = SFX_DIG;
                enemy_trap_check(dig_x, dig_y);
                return;
            }
        }
    }

    /*--- Process horizontal movement ---*/
    if (dx != 0 && is_aligned_y(p->py)) {
        new_gx = p->gx + dx;
        if (new_gx >= 0 && new_gx < GRID_COLS) {
            if (can_enter(tile_at(new_gx, p->gy))) {
                /* Must have support or be on bar/ladder to move horizontally */
                if (has_support(p->gx, p->gy) ||
                    is_on_bar(p->gx, p->gy) ||
                    is_on_ladder(p->gx, p->gy)) {

                    target_px = grid_to_pixel_x(new_gx);
                    p->px = move_toward(p->px, target_px, PLAYER_SPEED);

                    if (is_aligned_x(p->px))
                        p->gx = p->px / TILE_W;

                    p->dir = (dx < 0) ? DIR_LEFT : DIR_RIGHT;

                    if (is_on_bar(p->gx, p->gy))
                        p->state = PS_HANGING;
                    else
                        p->state = PS_RUNNING;

                    p->anim++;
                    pick_up_gold();
                    return;
                }
            }
        }
    }

    /*--- Process vertical movement ---*/
    if (dy != 0 && is_aligned_x(p->px)) {
        if (dy < 0) {
            /* Moving up - only on ladders */
            if (is_on_ladder(p->gx, p->gy)) {
                new_gy = p->gy - 1;
                if (new_gy >= 0 && can_enter(tile_at(p->gx, new_gy))) {
                    target_py = grid_to_pixel_y(new_gy);
                    p->py = move_toward(p->py, target_py, PLAYER_SPEED);

                    if (is_aligned_y(p->py))
                        p->gy = p->py / TILE_H;

                    p->state = PS_CLIMBING;
                    p->dir = DIR_UP;
                    p->anim++;
                    pick_up_gold();
                    return;
                }
            }
        } else {
            /* Moving down - on ladder, or release from bar */
            if (is_on_ladder(p->gx, p->gy) ||
                is_on_ladder(p->gx, p->gy + 1)) {
                new_gy = p->gy + 1;
                if (new_gy < GRID_ROWS && can_enter(tile_at(p->gx, new_gy))) {
                    target_py = grid_to_pixel_y(new_gy);
                    p->py = move_toward(p->py, target_py, PLAYER_SPEED);

                    if (is_aligned_y(p->py))
                        p->gy = p->py / TILE_H;

                    p->state = PS_CLIMBING;
                    p->dir = DIR_DOWN;
                    p->anim++;
                    pick_up_gold();
                    return;
                }
            }
            /* Release from bar - start falling */
            if (is_on_bar(p->gx, p->gy)) {
                p->state = PS_FALLING;
                return;
            }
        }
    }

    /*--- No input or can't move: update idle state ---*/
    if (is_aligned_x(p->px) && is_aligned_y(p->py)) {
        p->gx = p->px / TILE_W;
        p->gy = p->py / TILE_H;

        if (is_on_bar(p->gx, p->gy) && !has_support(p->gx, p->gy))
            p->state = PS_HANGING;
        else if (is_on_ladder(p->gx, p->gy))
            p->state = PS_CLIMBING;
        else
            p->state = PS_IDLE;
    } else {
        /* Continue sliding to grid alignment */
        target_px = grid_to_pixel_x(p->gx);
        target_py = grid_to_pixel_y(p->gy);
        p->px = move_toward(p->px, target_px, PLAYER_SPEED);
        p->py = move_toward(p->py, target_py, PLAYER_SPEED);
    }
}
