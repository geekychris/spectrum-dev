/*
 * The game's rules on the host, for the test's bot (tools/bot.py): the same
 * game.c and levels.c (game.h packs GameState on the host, so `gs` has the
 * Next's layout and is copied from the emulator).
 */
#include <stdint.h>
#include <string.h>
#include "game.h"

int sim_gs_size(void) { return sizeof(GameState); }
GameState *sim_gs(void) { return &gs; }

void sim_start(void)
{
    game_init();
    game_tick(INPUT_START);
}

uint8_t sim_tick(uint8_t input)
{
    game_changes = 0;
    game_tick(input);
    return game_changes;
}
