/*
 * The game's rules on the host, for the test's planner (tools/bot.py): the
 * same player.c, enemy.c, level.c and play.c (game.h packs its structs on
 * the host, so `gs` has the Spectrum's layout and is copied from the
 * emulator). tools/bot.py builds it with the host's cc.
 */
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "level.h"
#include "play.h"

GameState gs;
uint8_t sfx_pending;
extern uint32_t enemy_rng_state;

void render_tile_changed(uint8_t x, uint8_t y) { (void)x; (void)y; }

static uint8_t ready;

int sim_gs_size(void) { return sizeof(GameState); }
GameState *sim_gs(void) { return &gs; }
uint32_t *sim_rng(void) { return &enemy_rng_state; }

void sim_init(void)
{
    if (!ready) level_init();
    ready = 1;
}

void sim_load(int16_t level)
{
    sim_init();
    level_load(level);
}

/* a level of your own (tiles row-major, with player/enemy markers), as the
   editor's test play starts it */
void sim_custom(const uint8_t *tiles)
{
    sim_init();
    memcpy(gs.tiles, tiles, sizeof(gs.tiles));
    level_activate_spawn_markers();
    gs.gold_total = level_count_gold();
    gs.gold_collected = 0;
}

/* after gs was written from outside: the guards' path flags from the tiles */
void sim_sync(void)
{
    sim_init();
    level_refresh_paths();
}

uint8_t sim_tick(uint8_t held)
{
    return play_tick(held, 0);
}

/* run ticks with the same input; stops early on an event (returned) */
uint8_t sim_run(uint8_t held, uint16_t ticks)
{
    uint8_t r = PLAY_ON;
    while (ticks-- && (r = play_tick(held, 0)) == PLAY_ON) {}
    return r;
}
