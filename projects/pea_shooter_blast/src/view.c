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

extern void title_draw(void);
extern void sprites_build(const GameState *gs);

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

/* draw.c's draw_title, the parts that don't change (title.c, page 88) */
void view_title(void)
{
    l2_reset();
    ZXN_WRITE_MMU2(DRAW2_PAGE);
    title_draw();
    ZXN_WRITE_MMU2(LEVELS_PAGE);
}

void view_blank(void) { l2_reset(); }

/* ------------------------------------------------------------ sprites */

/* the frame's sprites (sprites.c, page 88) */
void view_sprites(const GameState *gs)
{
    ZXN_WRITE_MMU2(DRAW2_PAGE);
    sprites_build(gs);
    ZXN_WRITE_MMU2(LEVELS_PAGE);
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
