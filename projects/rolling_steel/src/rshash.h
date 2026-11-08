/* port: a hash of the race state after a step, the same on the host
   (tools/hostsim.c, the original's C) and the Next: FNV-1a over 32-bit words.
   Include after game.h. */
#ifndef RSHASH_H
#define RSHASH_H

#ifdef __Z88DK
extern unsigned long rs_mix(unsigned long h, unsigned long w);   /* rshash.asm */
#else
static unsigned long rs_mix(unsigned long h, unsigned long w)
{
    return (h ^ w) * 16777619UL;
}
#endif

static unsigned long rs_hash(unsigned long h)
{
    const Player *p = &G.p[0];
    int i;
    h = rs_mix(h, (unsigned long)(G.state | (G.level << 4) | (p->m.grounded << 8) | (p->deaths << 12)));
    h = rs_mix(h, p->m.p.x); h = rs_mix(h, p->m.p.y); h = rs_mix(h, p->m.p.z);
    h = rs_mix(h, p->m.v.x); h = rs_mix(h, p->m.v.y); h = rs_mix(h, p->m.v.z);
    h = rs_mix(h, p->m.w.x); h = rs_mix(h, p->m.w.y); h = rs_mix(h, p->m.w.z);
    h = rs_mix(h, p->time_left); h = rs_mix(h, p->course_time);
    for (i = 0; i < Wd.nenemy; i++) {
        h = rs_mix(h, Wd.enemy[i].b.p.x); h = rs_mix(h, Wd.enemy[i].b.p.y); h = rs_mix(h, Wd.enemy[i].b.p.z);
    }
    for (i = 0; i < 4; i++) { h = rs_mix(h, Wd.sweep_angle[i]); h = rs_mix(h, Wd.crush_h[i]); }
    for (i = 0; i < 4; i++) h = rs_mix(h, (unsigned long)Wd.crumble[i].state | ((unsigned long)Wd.crumble[i].timer << 8));
    return h;
}
#endif
