/* The Next's autopilot game on the host (test.py): the port's rules (game.c,
   the C versions of its asm) and bot.c, ticked as main.c ticks them; prints
   the hash lines main.c logs ("hash t=... h=...") for `ticks` ticks or to
   game over. Usage: hash_host <players> <ticks> */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "bot.h"

void sfx_gunfire(void) {}
void sfx_explosion(void) {}
void sfx_hit(void) {}
void sfx_die(void) {}

static GameWorld world;

static void log_hash(unsigned long t)
{
    printf("hash t=%lu h=%04x w=%d s=%ld,%ld l=%d,%d\n", t, ace_hash(&world), (int)world.wave,
           (long)world.players[0].score, (long)world.players[1].score,
           (int)world.players[0].lives, (int)world.players[1].lives);
}

int main(int argc, char **argv)
{
    int players = argc > 1 ? atoi(argv[1]) : 1;
    unsigned long n = argc > 2 ? strtoul(argv[2], 0, 10) : 10000, t;
    g_tune.num_players = players;
    game_init(&world);
    for (t = 1; t <= n; t++) {
        int was = world.state;
        UWORD in1 = ace_bot(&world, 0), in2 = players >= 2 ? ace_bot(&world, 1) : 0;
        game_update(&world, in1, in2);
        if (t % 1000 == 0) log_hash(t);
        if (world.state != was && world.state == STATE_GAMEOVER) { log_hash(t); break; }
    }
    return 0;
}
