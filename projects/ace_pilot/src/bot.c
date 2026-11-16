/*
 * bot.c - an autopilot (new: the original has none) and a hash of the game's
 * state. The autopilot turns toward the nearest enemy, climbs or dives to
 * its height and fires when it is close and ahead, as tools/trace.c's does
 * in doubles; here in integers (fix_atan2, an octagonal distance) so the
 * Z80 and the host get the same answers. The title's B key starts a game
 * flown by it, and the test runs it.
 */
#include <stdint.h>
#include "game.h"
#include "ace_input.h"
#include "zxgame.h"
#include "bot.h"

static int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

UWORD ace_bot(const GameWorld *w, uint8_t pi)
{
    const PlayerState *p = &w->players[pi];
    int32_t px, py, pz, bd = 0x7FFFFFFFL, dx, dz, dy, ax, az, horiz;
    int8_t best = -1, diff, elev, pnow, pd;
    uint8_t i, yaw, pitch, want;
    UWORD in = 0;
    if (!p->alive) return 0;
    px = p->cam.pos.x;                  /* port note: locals, not fields through pointers */
    py = p->cam.pos.y;
    pz = p->cam.pos.z;
    yaw = (uint8_t)p->cam.yaw;
    pitch = (uint8_t)p->cam.pitch;
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &w->enemies[i];
        int32_t d;
        if (!e->active) continue;
        ax = iabs32(e->pos.x - px);
        az = iabs32(e->pos.z - pz);
        d = ax > az ? ax + (az >> 1) : az + (ax >> 1);
        if (d < bd) { bd = d; best = (int8_t)i; }
    }
    if (best < 0) return INPUT_FIRE;
    {
        const Enemy *e = &w->enemies[(uint8_t)best];
        dx = e->pos.x - px;
        dz = e->pos.z - pz;
        dy = e->pos.y - py;
    }
    horiz = bd;
    {   /* scale to 16 bits, keeping the ratios */
        int32_t sx = dx, sz = dz;
        while (iabs32(sx) > 16000 || iabs32(sz) > 16000) { sx >>= 1; sz >>= 1; }
        want = fix_atan2((int16_t)sx, (int16_t)sz);     /* 0 = +z, 64 = +x: the rules' yaw */
    }
    diff = (int8_t)(uint8_t)(want - yaw);
    if (diff > 1) in |= INPUT_RIGHT;
    if (diff < -1) in |= INPUT_LEFT;
    {
        int32_t sy = dy, sh = horiz;
        uint8_t a;
        while (iabs32(sy) > 16000 || sh > 16000) { sy >>= 1; sh >>= 1; }
        a = fix_atan2((int16_t)sy, (int16_t)sh);        /* up = small positive */
        elev = (int8_t)(0 - (int8_t)a);                 /* the pitch that points at it (+ = down) */
    }
    if (elev > 32) elev = 32;
    if (elev < -32) elev = -32;
    pnow = (int8_t)pitch;
    pd = (int8_t)(elev - pnow);
    if (pd > 1) in |= INPUT_DOWN;
    if (pd < -1) in |= INPUT_UP;
    if (diff > -2 && diff < 2 && pd > -3 && pd < 3 && horiz < 600) in |= INPUT_FIRE;
    return in;
}

static uint16_t h16;
static void mix(int32_t v)
{
    uint16_t lo = (uint16_t)v, hi = (uint16_t)((uint32_t)v >> 16);
    h16 = (uint16_t)(h16 * 31u) + lo;
    h16 = (uint16_t)(h16 * 31u) + hi;
}

uint16_t ace_hash(const GameWorld *w)
{
    uint8_t i;
    h16 = 0x1234;
    mix(w->state); mix(w->wave); mix(w->enemies_alive); mix(w->enemies_to_spawn);
    for (i = 0; i < 2; i++) {
        const PlayerState *p = &w->players[i];
        mix(p->cam.pos.x); mix(p->cam.pos.y); mix(p->cam.pos.z);
        mix(p->cam.yaw); mix(p->cam.pitch);
        mix(p->score); mix(p->lives); mix(p->alive); mix(p->speed);
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &w->enemies[i];
        int16_t act = e->active;
        mix(act);
        if (!act) continue;
        mix(e->pos.x); mix(e->pos.y); mix(e->pos.z); mix(e->yaw); mix(e->health);
    }
    for (i = 0; i < MAX_BULLETS; i++) {
        const Bullet *b = &w->bullets[i];
        int16_t act = b->active;
        mix(act);
        if (act) { mix(b->pos.x); mix(b->pos.z); }
    }
    for (i = 0; i < MAX_GROUND; i++) mix(w->ground[i].active);
    return h16;
}
