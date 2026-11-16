/*
 * sprites.c - the frame's hardware sprites (nextsprl.h), in the original's
 * draw order (draw.c's draw_powerups ... draw_explosions). In page 88,
 * mapped at 0x4000 while it runs (view.c's view_sprites).
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextsprl.h"
#include "l2rle.h"
#include "psb.h"
#include "gfx_data.h"
#include "view.h"

#define W2S_X(wx, px) ((int16_t)(FIX_INT(wx) - (px)))
#define W2S_Y(wy)     ((int16_t)FIX_INT(wy))

static const uint8_t part_pal[16] = {   /* draw.c's colour -> sprite palette */
    5, 5, 4, 4, 4, 5, 2, 5, 1, 2, 3, 1, 2, 5, 3, 5
};

void sprites_build(const GameState *gs)
{
    int16_t px = (int16_t)gs->scroll.pixel_x, sx, sy;
    uint8_t i;
    spl_begin();
    spl_scale = 0;
    /* powerups (draw_powerups) */
    for (i = 0; i < MAX_POWERUPS; i++) {
        const Powerup *p = &gs->powerups[i];
        if (!p->active) continue;
        sx = W2S_X(p->x, px);
        sy = W2S_Y(p->y) + (p->anim_frame < 2 ? -1 : 1);
        if (sx < -16 || sx >= SCREEN_W) continue;
        spl_add(sx, sy, SPR_POWERUP + (p->type == POWERUP_WEAPON ? 0 : 1), 0, 0);
    }
    /* enemies (draw_enemies) */
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &gs->enemies[i];
        if (!e->active) continue;
        sx = W2S_X(e->x, px);
        sy = W2S_Y(e->y);
        if (sx < -16 || sx >= SCREEN_W + 16) continue;
        switch (e->type) {
        case ENEMY_WALKER: spl_add(sx, sy, SPR_WALKER, 0, 0); break;
        case ENEMY_FLYER: spl_add(sx, sy, SPR_FLYER + ((e->anim_frame & 1) ? 1 : 0), 0, 0); break;
        case ENEMY_TURRET: spl_add(sx, sy, SPR_TURRET, 0, e->facing ? SPR_MIRROR_X : 0); break;
        case ENEMY_HOPPER: spl_add(sx, sy, SPR_HOPPER + (e->state == 1), 0, 0); break;
        case ENEMY_BOSS: {
            int16_t max_hp = 30 + gs->level * 10, hp_w, k;
            spl_add(sx - 4, sy - 4, SPR_BOSS, 0, 0);
            spl_add(sx + 12, sy - 4, SPR_BOSS + 1, 0, 0);
            spl_add(sx - 4, sy + 12, SPR_BOSS + 2, 0, 0);
            spl_add(sx + 12, sy + 12, SPR_BOSS + 3, 0, 0);
            /* the health bar above it, 24 pixels in three sprites */
            hp_w = max_hp > 0 ? (e->health * 24) / max_hp : 0;
            if (hp_w < 0) hp_w = 0;
            if (hp_w > 24) hp_w = 24;
            for (k = 0; k < 3; k++) {
                int16_t g = hp_w - k * 8;
                spl_add(sx - 4 + k * 8, sy - 8, SPR_BAR + (g < 0 ? 0 : g > 8 ? 8 : g), 0, 0);
            }
            break;
        }
        }
    }
    /* enemy bullets, the tank's bullets */
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        const EnemyBullet *b = &gs->enemy_bullets[i];
        if (!b->active) continue;
        sx = W2S_X(b->x, px);
        if (sx < 0 || sx >= SCREEN_W) continue;
        spl_add(sx - 8, W2S_Y(b->y) - 8, SPR_EBULLET, 0, 0);
    }
    for (i = 0; i < MAX_BULLETS; i++) {
        const Bullet *b = &gs->bullets[i];
        if (!b->active) continue;
        sx = W2S_X(b->x, px);
        if (sx < 0 || sx >= SCREEN_W) continue;
        spl_add(sx - 8, W2S_Y(b->y) - 8, SPR_BULLET + b->size, 0, 0);
    }
    /* the tank, blinking while invulnerable (draw_tank) */
    {
        const Tank *t = &gs->tank;
        if (t->alive && !(t->invuln_timer > 0 && (t->invuln_timer & 4))) {
            sx = W2S_X(t->x, px);
            sy = W2S_Y(t->y);
            if (sx >= -TANK_W && sx < SCREEN_W) {
                spl_add(sx, sy, SPR_TANK + (t->anim_frame & 1), 0, t->facing ? SPR_MIRROR_X : 0);
                if (t->weapon_level > 0)
                    spl_add(sx, sy, SPR_DOTS + (t->weapon_level > 4 ? 4 : t->weapon_level), 0, 0);
            }
        }
    }
    /* particles: a pixel each */
    for (i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &gs->particles[i];
        if (!p->active) continue;
        sx = W2S_X(p->x, px);
        sy = W2S_Y(p->y);
        if (sx < 0 || sx >= SCREEN_W || sy < 0 || sy >= SCREEN_H) continue;
        spl_add(sx - 8, sy - 8, SPR_PIXEL, part_pal[p->color & 15], 0);
    }
    /* explosion flashes: draw.c's square of side 2r + 1, white then yellow;
       up to 61 pixels, so zoomed 2x or 4x when big */
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        const Explosion *e = &gs->explosions[i];
        int16_t r, side;
        uint8_t z = 0;
        if (!e->active) continue;
        sx = W2S_X(e->x, px);
        sy = W2S_Y(e->y);
        r = e->radius * e->life / 8;
        if (sx + r < 0 || sx - r >= SCREEN_W) continue;
        side = 2 * r + 1;
        while (side > 16) { side = (side + 1) >> 1; z++; }
        spl_scale = z;
        spl_add(sx - (8 << z), sy - (8 << z), SPR_SQUARE + side - 1, SPAL_PART + (e->life > 4 ? 4 : 2), 0);
        spl_scale = 0;
    }
    spl_end();
}

