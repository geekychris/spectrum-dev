// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * world_scene.c - world_scene(), the test hook that jumps to a hazard.
 * (port: split out of the original world.c; in bank 16 with world_traffic.c)
 */
#include <string.h>
#include "sc.h"
#include "world_int.h"

/* Put the car just before a hazard (or the finish) on whichever route
 * has one, for testing from the host. */
int world_scene(World *w, const char *name)
{
    int r, i, t, hz = -1, rt = w->route, s;

    if (strcmp(name, "finish")) {
        for (t = 1; t < HZ_COUNT && strcmp(name, hz_names[t]); t++)
            ;
        if (t >= HZ_COUNT) return 0;
        for (r = 0; r < NUM_ROUTES && hz < 0; r++) {
            rt = (w->route + r) % NUM_ROUTES;
            route_build(w, rt);
            for (i = 0; i < w->nhaz; i++)
                if (w->haz[i].type == t) { hz = i; break; }
        }
        if (hz < 0) return 0;
    }
    if (w->state == GS_TITLE || w->state == GS_GAMEOVER || w->state == GS_WIN) {
        w->score = 0;
        w->cover = 100;
        w->total_saves = w->total_needless = 0;
    }
    world_start_route(w, rt);
    if (hz < 0) s = w->finish - 12;
    else {
        s = w->haz[hz].type == HZ_SIREN ? w->haz[hz].seg - 1 : w->haz[hz].seg - 22;
        /* not in the middle of the hazard before it */
        for (i = 0; i < w->nhaz; i++)
            if (i != hz && s >= w->haz[i].seg - 4 && s <= w->haz[i].seg + w->haz[i].len + 2 &&
                w->haz[i].seg + w->haz[i].len + 3 < w->haz[hz].seg - 6)
                s = w->haz[i].seg + w->haz[i].len + 3;
    }
    if (s < 3) s = 3;
    w->car.z = (int32_t)s * SEGF;
    for (i = 0; i < w->nhaz; i++)
        if (w->haz[i].seg + w->haz[i].len + 2 < s)
            w->haz_state[i] = 2;
    w->state = GS_PLAY;
    w->nev = 0;
    return 1;
}
