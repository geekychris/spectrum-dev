/*
 * Bullion Dash - one frame of play: the STATE_PLAYING case of the original
 * main.c (input to the player, the guards, the bricks, then the checks).
 * Kept apart from main.c so the test's planner can run the same code on the
 * host (tools/hostsim.c).
 */
#include <stdint.h>
#include "game.h"
#include "play.h"
#include "player.h"
#include "enemy.h"
#include "level.h"
#include "zxgame.h"

uint8_t play_tick(uint8_t held, uint8_t quit)
{
    int8_t dx = (held & IN_LEFT) ? -1 : (held & IN_RIGHT) ? 1 : 0;
    int8_t dy = (held & IN_UP) ? -1 : (held & IN_DOWN) ? 1 : 0;
    uint8_t dig_left = 0, dig_right = 0;

    /* Digging: fire held + direction */
    if (held & IN_FIRE) {
        if (dx < 0) { dig_left = 1; dx = 0; }
        if (dx > 0) { dig_right = 1; dx = 0; }
    }

    player_update(dx, dy, dig_left, dig_right);
    enemy_update_all();
    level_tick_bricks();

    if (enemy_check_collision())
        return PLAY_CAUGHT;
    /* port: the original only noticed a guard; a player buried by a
       regrowing brick or fallen off the grid stood frozen until one came */
    if (gs.player.state == PS_DEAD)
        return PLAY_BURIED;
    /* port: BREAK gives up the life (some levels can't be finished) */
    if (quit)
        return PLAY_GAVE_UP;

    /* All gold collected - reveal hidden ladders (port: once) */
    if (gs.gold_collected >= gs.gold_total && gs.hidden_left) {
        level_reveal_hidden_ladders();
        gs.hidden_left = 0;
    }

    /* Win: all gold collected AND player at top row */
    if (gs.gold_collected >= gs.gold_total && gs.player.gy == 0)
        return PLAY_WON;
    return PLAY_ON;
}
