/* The rules on the host: the original's tests (run_tests) and the long
   autopilot game's hash, for tools/check_core.sh. */
#include <stdio.h>
#include "dc.h"
#include "vectors.h"
static int pass, fail;
static void check(int cond, const char *name)
{
    if (cond) pass++; else { fail++; printf("FAIL %s\n", name); }
}
int main(void)
{
    static DCGame g;
    uint32_t seeds[3] = {1, 2, 77}, s;
    int k, i;
    run_tests(check);
    printf("tests: %d passed, %d failed\n", pass, fail);
    for (i = 0; i < 3; i++) {
        s = seeds[i];
        dc_new_game(&g, s, 1);
        for (k = 0; k < 20000 && !g.over && !g.won; k++) dc_tick(&g, dc_bot(&g));
        printf("seed %lu: %d ticks day %d done %d fails %d score %ld hash %08lX\n", (unsigned long)s, k, g.day,
               g.jobs_done, g.fails, (long)g.score, (unsigned long)dc_state_hash(&g));
    }
    return 0;
}
