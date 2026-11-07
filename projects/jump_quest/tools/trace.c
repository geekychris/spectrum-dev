/* Host trace for checking the Spectrum build against the host one: plays
   level 1's route (tools/routes.json via stdin as "flags ticks" pairs) and
   prints the player each tick: "tick x y vy health". */
#include <stdio.h>
#include "jq.h"
int main(void)
{
    int f, n, t = 0;
    jq_new_game(CHAR_RJ, 1);
    while (scanf("%d %d", &f, &n) == 2)
        while (n--) {
            jq_tick((uint8_t)f);
            t++;
            printf("%d %d %d %d %d\n", t, game.player[0].x, game.player[0].y, game.player[0].vy, game.player[0].health);
        }
    return 0;
}
