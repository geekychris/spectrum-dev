/*
 * host_trace.c - the original's rules (src/game.c, src/levels.c, unchanged)
 * compiled for the host, playing state_hash.h's scripted game: prints the
 * hash the Next must reproduce (test.py compares).
 *
 *   sh tools/host_trace.sh
 */
#include <stdio.h>
#include "game.h"
#include "state_hash.h"

void sfx_shoot(void) {}
void sfx_hit(void) {}
void sfx_explode(void) {}
void sfx_powerup(void) {}
void sfx_jump(void) {}
void sfx_player_hit(void) {}

int main(void)
{
    static GameState gs;
    unsigned f;
    game_init(&gs);
    for (f = 0; f < SCRIPT_FRAMES && gs.state != STATE_TITLE; f++) {
        unsigned in = script_input((uint16_t)f);
        game_update(&gs, in & 1, (in >> 1) & 1, (in >> 2) & 1, (in >> 3) & 1, (in >> 4) & 1);
    }
    printf("%lx frames=%u level=%d score=%ld lives=%d\n", (unsigned long)state_hash(&gs), f, gs.level,
           (long)gs.score, gs.lives);
    return 0;
}
