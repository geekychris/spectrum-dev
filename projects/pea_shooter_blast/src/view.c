/*
 * view.c - Pea Shooter Blast's picture on the Spectrum Next (the original's
 * draw.c and the drawing half of main.c).
 *
 *   The playfield is Layer 2 in its 320 x 256 mode, scrolled sideways by
 *   the hardware (NextReg 0x16 / 0x71), with the SDK's l2scroll: its 320
 *   columns are a ring and only the map columns coming into view are drawn
 *   (at most 4 a frame: the original scrolls 4 pixels a frame at most), into
 *   the columns that just left it. A column is 16 copies of a 16-byte tile column (the tiles are
 *   stored column-major, like Layer 2; their page is mapped at 0x0000 over
 *   the rules, the Layer 2 column's at 0x6000 over the HUD). The original redraws all 22 x 16 tiles
 *   every frame.
 *   The background (pen 0) is a vertical gradient: the copper sets pen 0's
 *   colour every 4 lines, a different one for each level.
 *   Everything that moves is a hardware sprite (nextsprl.h), in the
 *   original's draw order; explosion flashes are squares zoomed by the
 *   hardware. The HUD is the tilemap (hud.c).
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextcopper.h"
#include "nextsprl.h"
#include "palfx.h"
#include "l2rle.h"
#include "l2scroll.h"
#include "psb.h"
#include "gfx_data.h"
#include "view.h"
#include "memmap.h"

#define L2_BANK   20                    /* Layer 2: banks 20-24 */
#define L2_PAGE   (L2_BANK * 2)
#define TILE_PAGE 68                    /* gfx/tiles.bin (bank 34) */
#define SPR_PAGE  70                    /* gfx/spr.bin (bank 35) */

uint16_t view_cols;                     /* map columns drawn last frame (profiling) */
static int16_t shown_px = -1;           /* the map x at the screen's left edge, -1: nothing drawn */
static int8_t shown_level = -1;
static uint8_t band;                    /* l2scroll's band: the whole screen */

/* ------------------------------------------------------------ the playfield */

/* l2scroll's fill: world (map) pixel column wx of the level into Layer 2
   column col (mapped by l2scroll): 16 slices of 16 bytes, one a tile */
static uint8_t fill_level;

static void column(uint8_t band, uint32_t wx, uint8_t *col)
{
    uint8_t r;
    uint16_t x = (uint16_t)wx;
    const uint8_t *m = &level_maps[fill_level][0][x >> 4], *tiles = (const uint8_t *)0x0000 + ((x & 15) << 4);
    (void)band;
    for (r = 0; r < MAP_H; r++, m += MAP_W, col += TILE_H)
        memcpy(col, tiles + ((uint16_t)*m << 8), TILE_H);
    view_cols++;
}

static void copper_gradient(uint8_t lvl)
{
    uint8_t k;
    const uint16_t *g = level_grad[lvl];
    copper_begin();
    for (k = 0; k < 64; k++) {
        copper_wait(L2Y_TO_LINE(k * 4), 0);
        copper_move(0x43, 0x10);                /* Layer 2's first palette */
        copper_move(0x40, 0);                   /* pen 0 */
        copper_move(0x41, (uint8_t)(g[k] >> 1));
    }
    copper_end();
}

void view_playfield(uint8_t lvl, int16_t px)
{
    uint8_t newlevel = lvl != shown_level;
    uint16_t off;
    if (!newlevel && px == shown_px) { view_cols = 0; return; }
    view_cols = 0;
    ZXN_WRITE_MMU0(TILE_PAGE);                  /* the tiles at 0x0000 (over the rules) */
    fill_level = lvl;
    if (newlevel || shown_px < 0) {
        copper_gradient(lvl);
        l2s_reset();
        band = l2s_band(0, SCREEN_H - 1, column);
        l2s_goto(band, (uint32_t)px);
        shown_level = (int8_t)lvl;
    } else l2s_move(band, (uint32_t)px);
    ZXN_WRITE_MMU0(RULES_PAGE);
    ZXN_WRITE_MMU3(UI_PAGE);
    /* one band over the whole screen: its scroll straight into the
       register (the copper does the gradient) */
    off = l2s_offset(band);
    ZXN_WRITE_REG(0x16, (uint8_t)off);
    ZXN_WRITE_REG(0x71, (uint8_t)(off >> 8));
    shown_px = px;
}

/* ------------------------------------------------------------ title / blank */

static uint8_t l2_on;

static void l2_reset(void)
{
    copper_stop();
    pal_set(1, 0, l2_pal[0]);                   /* pen 0: the original's background */
    ZXN_WRITE_REG(0x16, 0);
    ZXN_WRITE_REG(0x71, 0);
    l2w_clear(0);
    shown_px = -1;
    shown_level = -1;
}

static void fill(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c)    /* RectFill */
{
    l2w_fill(x0, (uint8_t)y0, x1 - x0 + 1, y1 - y0 + 1, c);
}

/* draw.c's draw_string at a scale: each pixel of the 5x7 font a
   scale x scale square */
extern const uint8_t font_5x7[96][7];
static void l2_string(int16_t x, int16_t y, const char *s, uint8_t sc, uint8_t c)
{
    for (; *s; s++, x += 6 * sc) {
        uint8_t g[7], r, col;
        memcpy(g, font_5x7[(uint8_t)*s - 32], 7);   /* the font is in page 84, which the fills map over */
        for (r = 0; r < 7; r++)
            for (col = 0; col < 5; col++)
                if (g[r] & (0x10 >> col)) l2w_fill(x + col * sc, (uint8_t)(y + r * sc), sc, sc, c);
    }
}

static void l2_centre(int16_t y, const char *s, uint8_t sc, uint8_t c)
{
    l2_string(SCREEN_W / 2 - ((int16_t)strlen(s) * 6 * sc - sc) / 2, y, s, sc, c);
}

/* draw.c's draw_title, the parts that don't change: the starfield, the
   title and the tank (the rest is the tilemap's) */
void view_title(void)
{
    uint32_t seed = 42;
    uint8_t i;
    int16_t cx = SCREEN_W / 2;
    l2_reset();
    for (i = 0; i < 40; i++) {
        seed = seed * 1103515245UL + 12345UL;
        l2w_plot((int16_t)((seed >> 16) % SCREEN_W), (uint8_t)((seed >> 8) % SCREEN_H), 1);
    }
    fill(cx - 20, 120, cx + 10, 134, 2);
    fill(cx - 14, 116, cx + 2, 122, 3);
    fill(cx + 2, 118, cx + 18, 120, 3);
    fill(cx - 22, 135, cx + 12, 140, 13);
    l2_centre(30, "PEA SHOOTER", 3, 8);
    l2_centre(60, "BLAST", 4, 10);
}

/* an empty screen (the high-score entry) */
void view_blank(void) { l2_reset(); }

/* ------------------------------------------------------------ sprites */

#define W2S_X(wx, px) ((int16_t)(FIX_INT(wx) - (px)))
#define W2S_Y(wy)     ((int16_t)FIX_INT(wy))

static const uint8_t part_pal[16] = {   /* draw.c's colour -> sprite palette */
    5, 5, 4, 4, 4, 5, 2, 5, 1, 2, 3, 1, 2, 5, 3, 5
};

void view_sprites(const GameState *gs)
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

void view_sprites_off(void)
{
    spl_begin();
    spl_end();
}

void view_init(void)
{
    palfx_load(PAL_L2, 0, 256, l2_pal);
    palfx_load(PAL_SPR, 0, 256, spr_pal);
    l2w_init(L2_BANK);
    l2w_slot = 3;
    l2w_restore = UI_PAGE;
    spl_init();
    ZXN_WRITE_MMU3(SPR_PAGE);
    spl_patterns(0, (const uint8_t *)0x6000, SPR_NPAT);
    ZXN_WRITE_MMU3(UI_PAGE);
    l2_reset();
}
