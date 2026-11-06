/* the statistics both simulators print (tools/host_sim.c: the Z80 port on
   the host; tools/orig_sim.c: the original core) */
#include <stdio.h>
#include <stdlib.h>
static long ev_count[64], games, steps_total, balls_total, max_sub, sum_sub, sub_n, escapes, stuck;
static double score_total;
static const char *ev_name[] = {
    "wall", "sling", "drop", "bumper", "flipperHit", "flipperStop", "spinnerHit", "spin",
    "sensor", "rampEnter", "ramp", "rampFail", "saucer", "kick", "laneRest", "drain", "plunge",
    "score", "skill", "lanesDone", "bank", "bankReset", "lock", "multiball", "jackpot",
    "missionStart", "missionDone", "missionFail", "supernova", "save", "endBall", "gameOver",
    "tiltWarn", "tilt", "nudge", "outlane",
};
static void report(const char *who)
{
    int i;
    double mins = steps_total / 120.0 / 60.0;
    printf("%s: %ld games, mean score %.0f, %.1f s a ball, substeps mean %.1f max %ld, escapes %ld, stuck %ld\n",
           who, games, score_total / games, steps_total / 120.0 / balls_total, (double)sum_sub / sub_n, max_sub, escapes, stuck);
    for (i = 0; i < 36; i++) if (ev_count[i]) printf("  %-13s %7.1f /min\n", ev_name[i], ev_count[i] / mins);
}
