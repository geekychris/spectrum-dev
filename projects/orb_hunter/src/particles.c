/*
 * particles.c - the original game.c's particles and the random numbers
 * they use (moved out of game.c, port: it runs from a 16K bank and needed
 * the room), unchanged.
 */
#include "game.h"

/* --- RNG --- */

static ULONG rng_state = 12345;

static WORD rng(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (WORD)((rng_state >> 16) & 0x7FFF);
}

/* --- Particles --- */

void game_spawn_particles(GameState *gs, Fixed x, Fixed y,
                          WORD count, WORD color)
{
    WORD i, spawned = 0;
    for (i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!gs->particles[i].active) {
            gs->particles[i].x = x;
            gs->particles[i].y = y;
            gs->particles[i].dx = (rng() % 512 - 256) * 128;
            gs->particles[i].dy = (rng() % 512 - 256) * 128;
            gs->particles[i].life = 10 + (rng() % 20);
            gs->particles[i].color = color;
            gs->particles[i].active = 1;
            spawned++;
        }
    }
}

void update_particles(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (!p->active) continue;
        p->x += p->dx;
        p->y += p->dy;
        p->dy += FIX(1) / 8;
        p->life--;
        if (p->life <= 0) p->active = 0;
    }
}

