/* Tank Duel for the Spectrum Next (work in progress: the rules check) */
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "td.h"
#include "fixmath.h"

static TDGame g;

/* the rules (bank 16 at 0x0000, bank 17 at 0x4000) */
static void map_rules(void)
{
    ZXN_WRITE_MMU0(32); ZXN_WRITE_MMU1(33);
    ZXN_WRITE_MMU2(34); ZXN_WRITE_MMU3(35);
}

int main(void)
{
    uint16_t t;
    TDEvent e;
    next_init();
    frame_init();
    zx_log("td: ready\n");
    map_rules();
    {   /* the fixed point against tools/fixcheck.py */
        uint32_t r = 1, h = 0, hh[5] = {0, 0, 0, 0, 0};
        uint16_t i;
        for (i = 0; i < 3000; i++) {
            fix a, b, m, d;
            r = r * 1103515245UL + 12345UL; a = (fix)r >> (r & 15);
            r = r * 1103515245UL + 12345UL; b = (fix)r >> (r & 15);
            if (!b) b = 1;
            m = fmul(a, b);
            d = fdiv(a >> 8, b);
            hh[0] = (hh[0] ^ (uint32_t)((uint32_t)m)) * 16777619UL;
            hh[1] = (hh[1] ^ (uint32_t)((uint32_t)d)) * 16777619UL;
            hh[2] = (hh[2] ^ (uint32_t)((uint32_t)a3_sin((uint16_t)r))) * 16777619UL;
            hh[3] = (hh[3] ^ (uint32_t)(a3_atan2(a >> 12, b >> 12))) * 16777619UL;
            hh[4] = (hh[4] ^ (uint32_t)((uint32_t)fhypot(a >> 4, b >> 4))) * 16777619UL;
            if (i < 3) zx_log("td: fix a=%ld b=%ld m=%ld d=%ld\n", (long)a, (long)b, (long)m, (long)d);
        }
        {
            static const uint32_t v[8] = {0xFFFFFFFFUL, 0xFFFE0001UL, 0xFFFE0000UL, 0x80000000UL, 0x7FFFFFFFUL,
                                          4232000000UL, 3000000000UL, 2116000000UL};
            uint8_t k;
            for (k = 0; k < 8; k++) zx_log("td: isqrt %lu=%u\n", v[k], isqrt32(v[k]));
        }
        zx_log("td: fixhash=%08lx %08lx %08lx %08lx %08lx\n", (unsigned long)hh[0], (unsigned long)hh[1], (unsigned long)hh[2], (unsigned long)hh[3], (unsigned long)hh[4]);
    }
    td_new_game(&g, 1234, 1);
    frame_ticks = 0;
    for (t = 0; t < 9000 && !g.over && !g.won; t++) {
        td_tick(&g, td_bot(&g));
        while (td_pop_event(&g, &e)) {}
        if (t % 1000 == 0) zx_log("td: tick %u frames %u\n", t, frame_ticks);
    }
    zx_log("td: trace round=%d score=%ld kills=%d hp=%d hash=%08lx\n", g.round, (long)g.score, g.kills,
           g.tk[0].hp, (unsigned long)td_state_hash(&g));
    {
        uint8_t k;
        for (k = 0; k < g.ntanks; k++)
            zx_log("td: tank %u alive=%d hp=%d x=%ld z=%ld yaw=%u v=%ld\n", k, g.tk[k].alive, g.tk[k].hp,
                   (long)g.tk[k].x, (long)g.tk[k].z, (uint16_t)g.tk[k].yaw, (long)g.tk[k].v);
        zx_log("td: tick=%ld shots=%d hits=%d dmg=%d nobs=%d\n", (long)g.tick, g.shots, g.hits, g.damage_taken, g.nobs);
    }
    zx_log("ZX:DONE\n");
    for (;;) frame_wait();
}
