/* keep_probe: the game's state in one packed struct for tests (test.py reads
   it by symbol; the host sim prints it). */
#include "keep.h"

Probe keep_probe;

void probe_update(void)
{
    Probe *p = &keep_probe;
    uint8_t i, n = 0;
    p->state = G.state;
    p->level = G.level_index;
    p->room = R.ridx;
    p->lives = (uint8_t)G.lives;
    p->relics = (uint8_t)G.relics;
    p->keys = (uint8_t)G.keys;
    p->paused = G.paused;
    p->npk = R.npk > 6 ? 6 : R.npk;
    p->player = (uint8_t)R.player;
    p->ngate = R.ngate;
    if (R.player >= 0) {
        const Body *b = R.act[R.player].body;
        p->px = b->px; p->py = b->py; p->pz = b->pz;
    }
    for (i = 0; i < R.nact && n < 10; i++) {
        const Body *b = R.act[i].body;
        if ((int8_t)i == R.player) continue;
        p->act[n].kind = R.act[i].kind;
        p->act[n].x = b->px; p->act[n].y = b->py; p->act[n].z = b->pz;
        n++;
    }
    p->nact = n;
    for (i = 0; i < p->npk; i++) {
        p->pk[i].kind = R.pk[i].kind;
        p->pk[i].x = R.pk[i].x; p->pk[i].y = R.pk[i].y; p->pk[i].z = R.pk[i].z;
    }
    p->tick++;
}
