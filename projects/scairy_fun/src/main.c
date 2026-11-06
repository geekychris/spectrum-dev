/* trace probe: the original's tests/trace_main.c for the 4 routes */
#include <arch/zxn.h>
#include "nextgfx.h"
#include "zxlog.h"
#include "sc.h"
extern void frame_init(void);

static World w;

int main(void)
{
    int f, route;
    Event e;
    ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33); ZXN_WRITE_MMU2(34); ZXN_WRITE_MMU3(35);
    next_init();
    frame_init();
    for (route = 0; route < 4; route++) {
        world_init(&w);
        w.tune.ai_perfect = 1;
        w.tune.traffic_pct = 50;
        world_start_game(&w);
        if (route) world_start_route(&w, route);
        w.state = GS_PLAY;
        zx_log("trace: route %d\n", route + 1);
        for (f = 0; f < 7500; f++) {
            world_update(&w, 0);
            while (world_pop_event(&w, &e))
                if (e.type != EV_SAY && e.type != EV_MULT_UP)
                    zx_log("f%ld %s %ld %ld\n", (long)f, ev_name[e.type], (long)e.a, (long)e.b);
            if (f % 250 == 0)
                zx_log("f%ld seg %ld d %ld v %ld rng %lu\n", (long)f, (long)seg_of(w.car.z),
                       (long)(w.car.d >> FBITS), (long)w.car.v, (unsigned long)w.rng);
            if (w.state != GS_PLAY) break;
        }
    }
    zx_log("trace: done\n");
    for (;;) ;
}
