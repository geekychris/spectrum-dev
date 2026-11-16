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
    uint16_t f;
    uint32_t h = hash_game(&gs, &f);
    printf("%lx frames=%u level=%d score=%ld lives=%d\n", (unsigned long)h, f, gs.level, (long)gs.score, gs.lives);
    return 0;
}
