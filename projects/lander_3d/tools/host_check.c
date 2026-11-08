/*
 * host_check.c - the port's rules (src/ld_core.c, on fix16 and the level
 * records) on the host: every level's start against the original's
 * ld_new_level() (built in as orig_ld_new_level from tools/orig, when
 * -DWITH_ORIG), and the flights of flight_hash.h against the original's
 * hashes (build/levels/expect.txt). tools/check_core.sh builds and runs it.
 *
 *   host_check LEVELDIR
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ld.h"
#include "lv_rec.h"
#include "../src/flight_hash.h"

void host_ter_load(const char *dir);
static Level L;

int main(int argc, char **argv)
{
    char path[512], line[200];
    FILE *f;
    int bad = 0, n = 0;
    if (argc < 2) return 2;
    host_ter_load(argv[1]);
    snprintf(path, sizeof(path), "%s/expect.txt", argv[1]);
    if (!(f = fopen(path, "r"))) { perror(path); return 1; }
    while (fgets(line, sizeof(line), f)) {
        int kind, w, lv;
        long steps, outcome, score;
        unsigned long h;
        int32_t st;
        uint32_t got;
        if (sscanf(line, "flight %d %d %d %ld %ld %ld %lx", &kind, &w, &lv, &steps, &outcome, &score, &h) != 7) continue;
        ld_new_level(&L, w, lv);
        got = flight_hash_kind(&L, kind, FLIGHT_STEPS, &st);
        n++;
        if (got != h || st != steps || L.outcome != outcome || L.result.score != score) {
            printf("MISMATCH flight %d %s %d: %ld steps %d %s score %ld hash %08lX (the original: %ld steps %s score %ld %08lX)\n",
                   kind, ld_worlds[w].name, lv, (long)st, L.outcome, ld_outcome_names[L.outcome], (long)L.result.score,
                   (unsigned long)got, steps, ld_outcome_names[outcome], score, h);
            bad++;
        } else
            printf("ok flight %d %-8s %2d: %5ld steps, %s, score %ld, hash %08lX\n", kind, ld_worlds[w].name, lv, (long)st,
                   ld_outcome_names[L.outcome], (long)L.result.score, (unsigned long)got);
    }
    fclose(f);
    printf("%d flights, %d mismatches\n", n, bad);
    return bad || !n;
}
