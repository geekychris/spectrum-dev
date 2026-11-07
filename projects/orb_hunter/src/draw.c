/*
 * draw.c - Orb Hunter's picture on the Next (the original's draw.c):
 *
 *   Layer 2, 320 x 256 (the original's screen), one buffer: the room's
 *   tiles are drawn when it is entered and only changed tiles, the lava
 *   and the save point are drawn again; the HUD in its top 16 rows when
 *   what it shows changes. Everything that moves is a hardware sprite
 *   (4-bit patterns made from the original's drawing, tools/gen_gfx.py),
 *   listed every frame with the SDK's nextsprl, clipped below the HUD.
 *   The title is a picture in banks of its own.
 */
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextspr.h"
#include "nextsprl.h"
#include "font5.h"
#include "zxgame.h"
#include "game.h"
#include "orb.h"
#include "banks.h"
#include "gfx_data.h"

/* the original's pens (main.c palette) */
enum { COL_BG, COL_WHITE, COL_HUNTER, COL_HUNTER2, COL_ROCK, COL_ROCK2, COL_METAL, COL_METAL2, COL_LAVA,
       COL_LAVA2, COL_MISSILE, COL_ENEMY, COL_ENEMY2, COL_DOOR, COL_ITEM, COL_BEAM };

static UBYTE drawn[ROOM_H][ROOM_W];            /* the tiles Layer 2 shows */
static struct { int32_t health, max, missiles; int8_t has_missiles, rx, ry, flash; } hud_shown;   /* what the HUD shows */
static uint8_t drawn_lava = 0xFF, drawn_save = 0xFF;

/* ------------------------------------------------------------ Layer 2 */
/* the tiles and patterns through slot 2 (Layer 2 is drawn through slot 3) */
static void gfx_in(void) { ZXN_WRITE_MMU2(GFX_BANK * 2); }
static void gfx_out(void) { ZXN_WRITE_MMU2(10); }

/* Layer 2 bank `bank` filled with colour c, a page at a time through slot 3 */
static void screen_clear_l2(uint8_t bank, uint8_t c)
{
    uint8_t p;
    for (p = 0; p < 10; p++) {
        ZXN_WRITE_MMU3(bank * 2 + p);
        dma_fill(WIN3, c, 8192);
    }
    ZXN_WRITE_MMU3(11);
}

extern void tile16(const uint8_t *src, uint8_t *dst) __z88dk_callee;   /* tile16.asm */

/* tile image n (column-major 16 x 16, in slot 2) at tile (tx, ty): its 16
   columns are in one Layer 2 page */
static void put_tile(uint8_t tx, uint8_t ty, uint8_t n)
{
    uint16_t x = tx * TILE_W;
    tile16(WIN2 + GFX_TILES_OFF + (uint16_t)n * 256, l2w_column(x) + ty * TILE_H + HUD_H);
}

static void clear_tile(uint8_t tx, uint8_t ty)
{
    uint16_t x = tx * TILE_W;
    uint8_t y = ty * TILE_H + HUD_H, c;
    for (c = 0; c < TILE_W; c++) memset(l2w_column(x + c) + y, COL_BG, TILE_H);
}

/* the tile image for a tile type (draw_tile); lava and the save point move */
static uint8_t image_of(UBYTE t)
{
    switch (t) {
    case TILE_ROCK: return T_ROCK;
    case TILE_BRICK: return T_BRICK;
    case TILE_METAL: return T_METAL;
    case TILE_PLATFORM: return T_PLATFORM;
    case TILE_LAVA: return T_LAVA0 + (gs.frame & 15);
    case TILE_DOOR_LOCKED: return T_DOOR;
    case TILE_SAVE_POINT: return T_SAVE0 + ((gs.frame / 8) & 1);   /* SAVE1: flash (ITEM), SAVE0: BEAM */
    }
    return 0xFF;                                /* empty, an open door, an item block (never placed) */
}

static void draw_tile_at(uint8_t tx, uint8_t ty)
{
    uint8_t n = image_of(room[ty][tx]);
    if (n == 0xFF) clear_tile(tx, ty);
    else put_tile(tx, ty, n);
    drawn[ty][tx] = room[ty][tx];
}

/* the bottom row (15) is under the screen's edge, as in the original */
#define ROWS_SHOWN (ROOM_H - 1)
#define MAX_ANIM 64
static uint8_t anim[MAX_ANIM], nanim;           /* the room's lava and save point (ty * 32 + tx) */
uint8_t room_dirty;                             /* world_set_tile changed a tile */

/* the whole screen cleared by DMA, then the room's tiles that aren't empty */
void draw_room_full(void)
{
    uint8_t tx, ty, t;
    nanim = 0;
    screen_clear_l2(L2_BANK, COL_BG);
    memset(&hud_shown, 0xFF, sizeof(hud_shown));
    gfx_in();
    for (ty = 0; ty < ROWS_SHOWN; ty++)
        for (tx = 0; tx < ROOM_W; tx++) {
            t = room[ty][tx];
            if (image_of(t) != 0xFF) draw_tile_at(tx, ty);
            else drawn[ty][tx] = t;
            if ((t == TILE_LAVA || t == TILE_SAVE_POINT) && nanim < MAX_ANIM) anim[nanim++] = ty * 32 + tx;
        }
    gfx_out();
    l2w_done();
    drawn_lava = gs.frame & 15;
    drawn_save = (gs.frame / 8) & 1;
    room_dirty = 0;
}

/* what changed since the last frame: an opened door, the lava's and the
   save point's next pictures */
void draw_room_update(void)
{
    uint8_t lava = gs.frame & 15, save = (gs.frame / 8) & 1, k, tx, ty;
    if (room_dirty) {
        gfx_in();
        for (ty = 0; ty < ROWS_SHOWN; ty++)
            for (tx = 0; tx < ROOM_W; tx++)
                if (room[ty][tx] != drawn[ty][tx]) draw_tile_at(tx, ty);
        room_dirty = 0;
    } else if (lava != drawn_lava || save != drawn_save) {
        gfx_in();
    } else {
        return;
    }
    for (k = 0; k < nanim; k++) {
        tx = anim[k] & 31;
        ty = anim[k] >> 5;
        if ((room[ty][tx] == TILE_LAVA && lava != drawn_lava) || (room[ty][tx] == TILE_SAVE_POINT && save != drawn_save))
            draw_tile_at(tx, ty);
    }
    gfx_out();
    l2w_done();
    drawn_lava = lava;
    drawn_save = save;
}

/* ------------------------------------------------------------ the HUD */
static const char *const area_names[] = { "CAVERN", "MAGMA", "BEAST DEN", "WYRM HOLLOW", "CITADEL", "SURFACE" };


static char *num(char *p, int32_t v, uint8_t digits)
{
    uint8_t k;
    for (k = digits; k; k--) { p[k - 1] = '0' + (char)(v % 10); v /= 10; }
    p[digits] = 0;
    return p;
}

static void hud_text(uint16_t x, uint8_t y, const char *s, uint8_t pen)
{
    l2w_text5(x, y, s, pen, 1, 1, 0);
}

/* the original's draw_hud: energy, missiles, the area, the map of rooms.
   port: its debug line (Y, F, R) is left out */
void draw_hud(void)
{
    char buf[8];
    Hunter *h = &gs.hunter;
    int8_t flash = (gs.frame & 4) != 0;
    if (hud_shown.health == h->health && hud_shown.max == h->max_energy && hud_shown.missiles == h->missiles &&
        hud_shown.has_missiles == h->has_missiles && hud_shown.rx == gs.room_x && hud_shown.ry == gs.room_y &&
        hud_shown.flash == flash)
        return;
    if (hud_shown.rx != gs.room_x || hud_shown.ry != gs.room_y || hud_shown.has_missiles != h->has_missiles ||
        hud_shown.max != h->max_energy || hud_shown.health != h->health || hud_shown.missiles != h->missiles) {
        int16_t w;
        l2w_fill(0, 0, SCREEN_W - 52 - 1, HUD_H - 1, COL_BG);
        hud_text(2, 1, "PW", COL_WHITE);
        l2w_fill(16, 1, 66, 7, COL_METAL2);
        w = h->max_energy > 0 ? (int16_t)((h->health * 64) / h->max_energy) : 0;
        if (w > 0) l2w_fill(17, 2, w, 5, h->health * 4 > h->max_energy ? COL_BEAM : COL_LAVA);
        hud_text(84, 1, num(buf, h->health < 0 ? 0 : h->health, 3), COL_WHITE);
        if (h->has_missiles) {
            hud_text(120, 1, "M", COL_MISSILE);
            hud_text(128, 1, num(buf, h->missiles, 2), COL_WHITE);
        }
        hud_text(160, 1, area_names[gs.room_y < 0 ? 0 : gs.room_y > 5 ? 5 : gs.room_y], COL_ITEM);
        l2w_fill(0, HUD_H - 1, SCREEN_W, 1, COL_METAL2);
    }
    /* the map of rooms: the current one flashing (its frame only when the room changes) */
    {
        uint16_t mx = SCREEN_W - 52;
        if (hud_shown.rx != gs.room_x || hud_shown.ry != gs.room_y) l2w_fill(mx - 1, 0, 8 * 6 + 2, 6 * 2 + 3, COL_METAL);
        l2w_fill(mx + gs.room_x * 6, 1 + gs.room_y * 2, 5, 2, flash ? COL_WHITE : COL_HUNTER);
    }
    l2w_done();
    hud_shown.health = h->health;
    hud_shown.max = h->max_energy;
    hud_shown.missiles = h->missiles;
    hud_shown.has_missiles = h->has_missiles;
    hud_shown.rx = gs.room_x;
    hud_shown.ry = gs.room_y;
    hud_shown.flash = flash;
}

/* ------------------------------------------------------------ sprites */
#define SY(fx) ((int16_t)FIX_INT(fx) + HUD_H)

static void quad(int16_t x, int16_t y, uint8_t first)
{
    spl_add(x, y, first, 0, 0);
    spl_add(x + 16, y, first + 1, 0, 0);
    spl_add(x, y + 16, first + 2, 0, 0);
    spl_add(x + 16, y + 16, first + 3, 0, 0);
}

static void draw_bomb(const Bomb *b)
{
    int16_t bx = FIX_INT(b->x), by = SY(b->y);
    if (b->timer > 10) {
        spl_add(bx, by, P_BOMB, 0, 0);
    } else {                                    /* the flash, radius 2..12 */
        int16_t r = (10 - b->timer) + 2, x0 = bx - r + 2, y0 = by - r + 2, x1 = bx + r, y1 = by + r;
        if (r <= 8) {
            spl_add(x0, y0, P_BLAST2 + r - 2, 0, 0);
        } else {                                /* four mirrored corners */
            spl_add(x0, y0, P_BLAST_CORNER, 0, 0);
            spl_add(x1 - 15, y0, P_BLAST_CORNER, 0, SPR_MIRROR_X);
            spl_add(x0, y1 - 15, P_BLAST_CORNER, 0, SPR_MIRROR_Y);
            spl_add(x1 - 15, y1 - 15, P_BLAST_CORNER, 0, SPR_MIRROR_X | SPR_MIRROR_Y);
        }
    }
}

/* draw_frame's order: items, hunter, bullets, bombs, enemies, enemy
   bullets, particles (later ones on top) */
void draw_sprites(void)
{
    WORD i, f = gs.frame;
    Hunter *h = &gs.hunter;
    spl_begin();
    for (i = 0; i < g_num_items; i++) {
        const ItemSpawn *it = &g_items[i];
        if ((gs.items_collected & (1L << i)) || it->room_x != gs.room_x || it->room_y != gs.room_y) continue;
        spl_add(it->tile_x * TILE_W, it->tile_y * TILE_H + HUD_H, ((f / 4) + i) & 2 ? P_ORB1 : P_ORB0, 0, 0);
    }
    /* port: the invulnerable hunter flickers on the shown frames (the
       original's test of its frame counter hid it, ORIGINAL_BUGS.md) */
    if (h->alive && !(h->invuln_timer > 0 && (show_frame & 1))) {
        int16_t sx = FIX_INT(h->x) - 2, sy = SY(h->y);
        if (h->rolled) spl_add(sx, sy, P_ROLL0 + ((f / 2) & 3), 0, 0);
        else spl_add(sx, sy, h->facing ? P_HUNTER_L : P_HUNTER_R, 0, 0);
    }
    for (i = 0; i < MAX_BULLETS; i++) {
        const Bullet *b = &gs.bullets[i];
        if (!b->active) continue;
        if (b->type == BULLET_TYPE_MISSILE) spl_add(FIX_INT(b->x), SY(b->y), b->dx > 0 ? P_MISSILE_R : P_MISSILE_L, 0, 0);
        else spl_add(FIX_INT(b->x), SY(b->y), P_BEAM, 0, 0);
    }
    for (i = 0; i < MAX_BOMBS; i++) if (gs.bombs[i].active) draw_bomb(&gs.bombs[i]);
    /* port: enemies are always shown (the original's flash test hid them, ORIGINAL_BUGS.md) */
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &gs.enemies[i];
        int16_t ex, ey;
        if (!e->active) continue;
        ex = FIX_INT(e->x);
        ey = SY(e->y);
        switch (e->type) {
        case ENEMY_CRAWLER: spl_add(ex, ey, e->facing ? P_CRAWLER_L : P_CRAWLER_R, 0, 0); break;
        case ENEMY_FLYER: spl_add(ex, ey, P_FLYER0 + ((f / 3) & 1), 0, 0); break;
        case ENEMY_HOPPER: spl_add(ex, ey, P_HOPPER, 0, 0); break;
        case ENEMY_TURRET: spl_add(ex, ey, P_TURRET0 + ((f / 8) & 1), 0, 0); break;
        case ENEMY_BOSS_BEAST: quad(ex, ey, P_BEAST0); break;
        case ENEMY_BOSS_DRAKE: quad(ex, ey, (f / 4) & 1 ? P_DRAKE1_0 : P_DRAKE0_0); break;
        }
    }
    for (i = 0; i < MAX_ENEMY_BULLETS; i++)
        if (gs.enemy_bullets[i].active) spl_add(FIX_INT(gs.enemy_bullets[i].x), SY(gs.enemy_bullets[i].y), P_EBULLET, 0, 0);
    for (i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &gs.particles[i];
        if (p->active) spl_add(FIX_INT(p->x), SY(p->y), P_SPARK, (uint8_t)p->color, 0);   /* group k: pen 1 = colour k */
    }
    spl_end();
}

void draw_hide_sprites(void)
{
    spl_begin();
    spl_end();
}

/* ------------------------------------------------------------ setup */
void draw_init(void)
{
    uint8_t k;
    pal_load(1, l2_pal, 256);
    pal_load(2, spr_pal, 256);
    screen_clear_l2(L2_BANK, COL_BG);
    l2w_init(L2_BANK);
    /* the sprites: below the HUD only (over-border mode: x in pairs, y 0..255) */
    spr_init();
    spl_init();
    ZXN_WRITE_REG(0x1C, 0x02);
    ZXN_WRITE_REG(0x19, 0);
    ZXN_WRITE_REG(0x19, 159);
    ZXN_WRITE_REG(0x19, HUD_H);
    ZXN_WRITE_REG(0x19, 255);
    gfx_in();
    for (k = 0; k < GFX_NPATS; k += 16)
        spl_patterns(k, WIN2 + GFX_PATS_OFF + (uint16_t)k * 128, GFX_NPATS - k < 16 ? GFX_NPATS - k : 16);
    gfx_out();
    memset(drawn, 0xFF, sizeof(drawn));
    memset(&hud_shown, 0xFF, sizeof(hud_shown));
}

void show_game(void)
{
    ZXN_WRITE_REG(0x12, L2_BANK);
    memset(&hud_shown, 0xFF, sizeof(hud_shown));
}

/* ------------------------------------------------------------ screens */
static void text_centre(uint8_t y, const char *s, uint8_t pen, uint8_t scale)
{
    uint16_t w = font5_width(s, scale);
    l2w_text5((SCREEN_W - w) / 2, y, s, pen, scale, scale, 0);
}

/* the title picture (gen_gfx.py), with the controls and the high scores
   drawn on it (port: the table is new, the controls are the Spectrum's) */
void draw_title(void)
{
    static const char *const controls[] = { "CONTROLS:", "O/P - MOVE", "Q - JUMP / AIM UP", "A - ROLL FORM",
                                            "SPACE - SHOOT/BOMB", "Q+SPACE - MISSILE", "KEMPSTON TOO" };
    char line[16];
    uint8_t k;
    draw_hide_sprites();
    l2w_bank = TITLE_BANK;
    l2w_fill(56, 146, 214, 68, COL_BG);
    for (k = 0; k < 7; k++) l2w_text5(60, 150 + k * 9, controls[k], k ? COL_METAL2 : COL_WHITE, 1, 1, 0);
    l2w_text5(186, 150, "HIGH SCORES", COL_WHITE, 1, 1, 0);
    for (k = 0; k < HS_ENTRIES; k++) {
        line[0] = '1' + k;
        line[1] = ' ';
        memcpy(line + 2, hs_table[k].name, 3);
        line[5] = ' ';
        num(line + 6, hs_table[k].score, 6);
        l2w_text5(186, 162 + k * 10, line, k ? COL_METAL2 : COL_MISSILE, 1, 1, 0);
    }
    l2w_done();
    l2w_bank = L2_BANK;
    ZXN_WRITE_REG(0x12, TITLE_BANK);
}

/* "PRESS FIRE TO START", flashing (draw_title's (g_frame / 20) & 1) */
void draw_title_prompt(uint8_t on)
{
    l2w_bank = TITLE_BANK;
    l2w_fill(0, 128, SCREEN_W, 9, COL_BG);
    if (on) text_centre(130, "PRESS FIRE TO START", COL_WHITE, 1);
    l2w_done();
    l2w_bank = L2_BANK;
}

/* the original's draw_item_get box, over the room */
void draw_item_get(void)
{
    char buf[48];
    uint16_t bx = (SCREEN_W - 200) / 2;
    uint8_t by = (SCREEN_H - 60) / 2;
    l2w_fill(bx, by, 200, 60, COL_ITEM);
    l2w_fill(bx + 1, by + 1, 198, 58, COL_BG);
    strcpy(buf, "GOT ");
    strcat(buf, gs.item_name);
    strcat(buf, "!");
    text_centre(by + 14, buf, COL_WHITE, 1);
    text_centre(by + 44, "PRESS FIRE", COL_METAL2, 1);
    l2w_done();
    memset(drawn, 0xFF, sizeof(drawn));             /* the room comes back after it */
    room_dirty = 1;
}

/* the original's draw_gameover */
void draw_gameover(uint8_t prompt)
{
    static uint8_t shown;
    if (prompt == 2) {                          /* the screen */
        draw_hide_sprites();
        screen_clear_l2(L2_BANK, COL_BG);
        text_centre(80, "GAME OVER", COL_LAVA, 3);
        l2w_fill((SCREEN_W - font5_width("GAME OVER", 3)) / 2, 107, font5_width("GAME OVER", 3), 2, COL_LAVA2);
        memset(drawn, 0xFF, sizeof(drawn));
        memset(&hud_shown, 0xFF, sizeof(hud_shown));
        shown = 0;
    } else if (prompt != shown) {
        l2w_fill(0, 160, SCREEN_W, 8, COL_BG);
        if (prompt) text_centre(160, "PRESS FIRE TO CONTINUE", COL_WHITE, 1);
        shown = prompt;
    }
    l2w_done();
}

/* port: the initials for the high-score table, on the game over screen */
void draw_name_entry(const char *name, uint8_t pos, int8_t rank)
{
    char buf[28];
    uint8_t n;
    l2w_fill(0, 140, SCREEN_W, 60, COL_BG);
    strcpy(buf, "NEW HIGH SCORE - PLACE ");
    n = (uint8_t)strlen(buf);
    buf[n] = '1' + rank;
    buf[n + 1] = 0;
    text_centre(140, buf, COL_MISSILE, 1);
    text_centre(156, name, COL_WHITE, 2);
    l2w_fill((SCREEN_W - font5_width(name, 2)) / 2 + pos * 12, 172, 10, 2, COL_ITEM);
    text_centre(186, "TYPE YOUR INITIALS", COL_METAL2, 1);
    l2w_done();
}
