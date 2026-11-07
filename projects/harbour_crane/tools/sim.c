/* The rules (src/hc_core.c on fix16) on the host as a shared library, for
   tools/bot.py's development (tools/botsim.py):
     cc -O2 -shared -fPIC -Isrc -I../../sdk/include -o build/host/libhc.so tools/sim.c src/hc_core.c \
        ../../sdk/src/fix16.c ../../sdk/src/fix16_tab.c
   probe() writes the same bytes as src/game.c's probe(). */
#include <stdint.h>
#include "hc.h"

HCGame g;
uint8_t hc_probe[25];

void sim_new(uint32_t seed, int level) { hc_new_game(&g, seed, level); }

int sim_tick(unsigned in)
{
    HCEvent e;
    int ev = 0;
    hc_tick(&g, in);
    while (hc_pop_event(&g, &e)) ev |= 1 << e.type;
    return ev;
}

unsigned sim_bot(void) { return hc_bot(&g); }

void probe(void)
{
    uint8_t *p = hc_probe;
    int16_t v;
    *p++ = 1;
    v = (int16_t)(g.tx >> 16); *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8);
    v = (int16_t)(hc_hook_x(&g) >> 16); *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8);
    v = (int16_t)(hc_hook_y(&g) >> 16); *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8);
    *p++ = (uint8_t)g.carrying;
    *p++ = (uint8_t)g.falling;
    *p++ = (uint8_t)g.next;
    *p++ = (uint8_t)g.nbox;
    *p++ = (uint8_t)g.loaded;
    *p++ = (uint8_t)g.lost;
    *p++ = (uint8_t)g.level;
    *p++ = (uint8_t)g.over;
    *p++ = (uint8_t)g.won;
    *p++ = 0;
    *p++ = (uint8_t)g.score; *p++ = (uint8_t)(g.score >> 8);
    *p++ = (uint8_t)(g.score >> 16); *p++ = (uint8_t)(g.score >> 24);
    v = (int16_t)(g.tv >> 8); *p++ = (uint8_t)v; *p++ = (uint8_t)(v >> 8);
    *p++ = (uint8_t)g.between;
    *p = 1;
}
