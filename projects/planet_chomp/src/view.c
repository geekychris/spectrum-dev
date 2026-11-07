/*
 * The sprites' positions and the radar (the original's main_68k.c
 * build_sprites and radar), in the render phase with render.c's camera.
 * Page 41 at 0x6000 (main.c maps it after drawing).
 */
#include "pc.h"
#include "game.h"
#include "render.h"
#include "actors.h"
#include "spr_data.h"

/* actor a shows texture tex, size half (world Q8) at world p, if it is on
   this side of the horizon (slack: it pokes up over it) */
static void place(uint8_t a, const V3 *p, uint8_t tex, int16_t half)
{
    Act *ac = &act[a];
    int16_t sx, sy, z, d, best = 0x7FFF;
    uint8_t s, k;
    ac->show = 0;
    if (half <= 0) return;
    /* round the far side: dir . C < R - 0.5, with p = dir (R + ~0.5) (port: no
       normalising) */
    if (v3_dot32(p, &rd_campos) < (int32_t)(PLANET_R - Q8(0.5)) * (PLANET_R + Q8(0.5))) return;   /* (Q16) */
    if (!rd_project(p, &sx, &sy, &z)) return;
    d = (rd_size16(half, z) + 4) >> 3;                          /* pixels across */
    s = 0;
    for (k = 0; k < SPR_SIZES; k++) {
        int16_t e = d - spr_size[k];
        if (e < 0) e = -e;
        if (e < best) { best = e; s = k; }
    }
    if (sx < -32 || sx > RD_W + 32 || sy < -32 || sy > RD_H + 32) return;
    ac->tex = tex;
    ac->size = s;
    ac->x = 32 + sx - (s < SPR_SMALL ? 8 : 16);                 /* the sprite plane: Layer 2 at 32, 32 */
    ac->y = 32 + sy - (s < SPR_SMALL ? 8 : 16);
    ac->show = 1;
}

/* the chomper's facing on screen: 0 right, 1 up, 2 left, 3 down */
static uint8_t chomper_dir(void)
{
    int16_t x0, y0, x1, y1, z, dx, dy;
    if (!rd_project(&pv.pworld, &x0, &y0, &z) || !rd_project(&pv.pfront, &x1, &y1, &z)) return 0;
    dx = x1 - x0;
    dy = y1 - y0;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)) return dx > 0 ? 0 : 2;
    return dy < 0 ? 1 : 3;
}

/* a radar dot: the whole sphere round the chomper, the centre here, the rim
   the far side, in the camera's frame */
static void radar_dot(uint8_t i, const V3 *d, const V3 *cr, uint8_t pat)
{
    int16_t cosang = v3_dot(&pv.pdir, d), x, y, a;
    V3 t, u;
    v3_scale(&u, &pv.pdir, cosang);
    v3_sub(&t, d, &u);
    if (v3_zero(&t)) { x = y = 0; }
    else {
        v3_norm(&t, &t);
        a = (ONE14 - cosang) / 2;
        a = (int16_t)((a * 3L + isqrt32((uint32_t)a << 14)) / 4);
        x = mulq14(v3_dot(&t, cr), a);
        y = mulq14(v3_dot(&t, &g.cam.up), a);
    }
    dot[i].x = 282 + (int16_t)(mul16(x, 30) >> 14);
    dot[i].y = 214 - (int16_t)(mul16(y, 30) >> 14);
    dot[i].pat = pat;
}

static uint8_t radar_phase;

void rd_sprites(void)
{
    uint8_t i, playing = g.state != GS_TITLE && g.state != GS_OVER, n = 0;
    V3 p, cr;
    /* the chomper */
    act[0].show = 0;
    if (playing) {
        uint8_t mouth = 1;
        int16_t half = Q8(0.55);
        if (g.player.moving) {
            uint8_t ph = g.mouth % 12;
            mouth = ph < 4 ? 0 : ph < 8 ? 1 : 2;
        }
        if (g.state == GS_DYING) {
            int32_t t = g.timer - TICKS(0.4);
            mouth = 2;
            if (t > 0) half = (int16_t)((int32_t)half * (TICKS(1.4) - (t > TICKS(1.4) ? TICKS(1.4) : t)) / TICKS(1.4));
        }
        place(0, &pv.pworld, (uint8_t)(g.powered * 12 + chomper_dir() * 3 + mouth), half);
    }
    /* the spooks */
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        uint8_t t;
        act[1 + i].show = 0;
        if ((g.state == GS_DYING && g.timer > TICKS(0.4)) || g.state == GS_CLEAR || g.state == GS_OVER) continue;
        if (gh->mode == GM_EATEN) t = 24 + 6;                      /* GT_EYES */
        else if (gh->fright)
            t = (g.power_left < TICKS(2) && (g.power_left % 20) < 10) ? 24 + 5 : 24 + 4;   /* GT_FLASH, GT_FRIGHT */
        else t = 24 + i;
        place(1 + i, &pv.gworld[i], t, Q8(0.6));
    }
    /* the keys */
    for (i = 0; i < 4; i++) {
        act[5 + i].show = 0;
        if (!key[mz_keys[i]]) continue;
        v3_scale(&p, &mz_spot[i], PLANET_R + Q8(0.45));
        place(5 + i, &p, 31, Q8(0.42));
    }
    /* score popups float up from eaten spooks */
    for (i = 0; i < POP_N; i++) {
        int16_t sx, sy, z;
        pop[i].pat = 0;
        if (g.popup[i].age < 0 || !rd_project(&g.popup[i].world, &sx, &sy, &z)) continue;
        pop[i].x = 32 + sx - 16;
        pop[i].y = 32 + sy - 8 - 8 - g.popup[i].age * 2 / 5;
        pop[i].pat = g.popup[i].pts == 200 ? 1 : g.popup[i].pts == 400 ? 2 : g.popup[i].pts == 800 ? 3 : 4;
    }
    /* the radar (port: every 4th frame) */
    i = g.lives - ((g.state == GS_READY || g.state == GS_PLAYING) ? 1 : 0);
    lives_shown = g.state == GS_TITLE || g.state == GS_OVER ? 0 : i > 8 ? 8 : i;
    if (g.state == GS_TITLE || g.state == GS_OVER) {
        for (i = 0; i < DOT_N; i++) dot[i].pat = 0;
        return;
    }
    if (++radar_phase & 3) return;
    for (i = 0; i < DOT_N; i++) dot[i].pat = 0;
    v3_cross(&cr, &g.cam.focus, &g.cam.up);
    v3_norm(&cr, &cr);
    for (i = 0; i < 4; i++)
        if (key[mz_keys[i]]) radar_dot(n++, &mz_spot[i], &cr, DOT_KEY + 1);
    radar_dot(n++, &mz_spot[4], &cr, DOT_NEST + 1);
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        radar_dot(n++, &pv.gdir[i], &cr, (gh->mode == GM_EATEN ? DOT_EYES : gh->fright ? DOT_FRIGHT : i) + 1);
    }
    dot[n].x = 282;
    dot[n].y = 214;
    dot[n].pat = DOT_HERO + 1;
}
