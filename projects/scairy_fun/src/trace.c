/*
 * trace.c - the original's tests/trace_main.c on the Z80 (make TRACE=1;
 * tools/check_trace.sh diffs it with the original built on the host): the
 * four routes driven by the perfect autopilot at 50% and 100% traffic, every
 * event and the car every 250 steps.
 */
#ifdef SC_TRACE
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "sc.h"
#include "game.h"

World world;
uint8_t render_skip, sfx_queue;
static volatile uint16_t ints;
static void tick(void) { ints++; }

void map_sim(void)
{
    ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33);
    ZXN_WRITE_MMU2(34); ZXN_WRITE_MMU3(35);
}

int main(void)
{
    static const int traffic[2] = { 50, 100 };
    World *w = &world;
    int f, route, pass;
    Event e;
    map_sim();
    next_init();
    frame_init();
    frame_hook = tick;
    for (pass = 0; pass < 2; pass++)
        for (route = 0; route < 4; route++) {
            world_init(w);
            w->tune.ai_perfect = 1;
            w->tune.traffic_pct = traffic[pass];
            world_start_game(w);
            if (route) world_start_route(w, route);
            w->state = GS_PLAY;
            zx_log("trace: route %d ints=%u\n", route + 1, ints);
            for (f = 0; f < 7500; f++) {
                world_update(w, 0);
                while (world_pop_event(w, &e))
                    if (e.type != EV_SAY && e.type != EV_MULT_UP)
                        zx_log("f%ld %s %ld %ld\n", (long)f, ev_name[e.type], (long)e.a, (long)e.b);
                if (f % 250 == 0)
                    zx_log("f%ld seg %ld d %ld v %ld rng %lu\n", (long)f, (long)seg_of(w->car.z),
                           (long)(w->car.d >> FBITS), (long)w->car.v, (unsigned long)w->rng);
                if (w->state != GS_PLAY) break;
            }
        }
    zx_log("trace: done ints=%u\n", ints);
    for (;;) ;
}
#endif
