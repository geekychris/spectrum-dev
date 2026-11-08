/*
 * ap_rate.c - the autopilot deciding every N steps (game_rules.c's demo and
 * B: port: the original decides every step) flown over every level on the
 * host: each must still land on a pad. Prints each level's outcome.
 *
 *   ap_rate LEVELDIR N
 *
 * Built from projects/lander_3d like tools/check_core.sh's host_check:
 *   S=../../sdk/src; cc -O2 -Isrc -I../../sdk/include tools/ap_rate.c
 *     tools/host_ter.c src/ld_core.c src/ld_land.c src/ld_level.c
 *     src/ld_auto.c $S/fix16.c $S/fix16_tab.c $S/fix16_atan.c
 *     $S/fix16_hyp3.c -o ap_rate
 */
#include <stdio.h>
#include <stdlib.h>
#include "ld.h"
#include "lv_rec.h"

void host_ter_load(const char *dir);
static Level L;

int main(int argc, char **argv)
{
    int w, lv, n, bad = 0;
    if (argc < 3) return 2;
    host_ter_load(argv[1]);
    n = atoi(argv[2]);
    for (w = 0; w < W_COUNT; w++)
        for (lv = 1; lv <= LV_PER_WORLD; lv++) {
            LdInput in = { 0, 0, 0 };
            long t;
            ld_new_level(&L, w, lv);
            for (t = 0; t < 30000 && !L.outcome; t++) {
                if (t % n == 0) ld_autopilot(&L, &in);
                ld_step(&L, &in);
            }
            printf("%-8s %2d: %6ld steps %-7s score %ld\n", ld_worlds[w].name, lv, t, ld_outcome_names[L.outcome],
                   (long)L.result.score);
            if (L.outcome != OUT_LANDED) bad++;
        }
    printf("every %d steps: %d of %d levels not landed\n", n, bad, W_COUNT * LV_PER_WORLD);
    return bad != 0;
}
