/*
 * Orbital Patrol on the Next: the drawing (the original's draw.c; see
 * view.h). Layer 2 is in its 320 x 256 mode in banks 16-20, mapped a
 * column page at a time into MMU slot 1 (0x2000, over the ROM, whose font
 * is copied to RAM at the start). The stars' tilemap is in bank 5 at
 * 0x4000 (map) and 0x4500 (tiles), below the program.
 */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "nextcopper.h"
#include "nextsplit.h"
#include "view.h"
#include "gfx_data.h"

#define L2_BANK     16
#define ROW_PLAY    20          /* the scrolled rows: PLAY_TOP .. */
#define ROW_HUD     237         /* .. up to here (the terrain reaches row 235) */
#define TM_MAP      ((uint8_t *)0x4000)     /* 40 x 32, a byte a cell */
#define TM_TILES    ((uint8_t *)0x4500)     /* 32 bytes a tile */
#define TM_MAX_TILES 32

uint16_t view_cols_drawn;
uint8_t view_sprites_used;

/* ------------------------------------------------------------ Layer 2 columns */

/* view's big buffers share page 46 (always at 0x0000) with the game's state */
typedef char gs_fits_below_0x1600[(sizeof(GameState) <= 0x1600) ? 1 : -1];
#define font   ((uint8_t *)0x1600)          /* the ROM's, characters 32..127: 768 */
#define col_wx ((int16_t *)0x1900)          /* 320 x 2 (the terrain, below) */
#define col_h  ((uint8_t *)0x1B80)          /* 320 */
#define attrs  ((uint8_t *)0x1CC0)          /* 128 x 4: the sprites, sent by DMA */
#define scan_ytab ((uint8_t *)0x1EC0)       /* 256: the scanner row of each world row */
#define ATTRS_LEN (128 * 4)

static uint8_t *column(uint16_t x)          /* physical column x, row 0 */
{
    ZXN_WRITE_REG(0x51, L2_BANK * 2 + (x >> 5));
    return (uint8_t *)(0x2000 + ((x & 31) << 8));
}

static void columns_done(void)
{
    ZXN_WRITE_REG(0x51, 0xFF);              /* the ROM back */
}

static const uint8_t *glyph(char c)
{
    uint8_t i = (uint8_t)c - 32;
    return font + (i < 96 ? i : 0) * 8;
}

static uint16_t scroll;                     /* the playfield's Layer 2 scroll (physical column at screen 0) */
static uint8_t scrolled;                    /* rows ROW_PLAY..ROW_HUD-1 scroll */

/* the physical column showing screen column sx in row y */
static uint16_t phys(int16_t sx, uint8_t y)
{
    uint16_t p;
    if (!scrolled || y < ROW_PLAY || y >= ROW_HUD) return (uint16_t)sx;
    p = (uint16_t)sx + scroll;
    return p >= 320 ? p - 320 : p;
}

void view_fill(int16_t sx, uint8_t y, uint16_t w, uint8_t h, uint8_t colour)
{
    for (; w; w--, sx++) {
        if (sx < 0 || sx >= 320) continue;
        memset(column(phys(sx, y)) + y, colour, h);
    }
    columns_done();
}

/* ROM-font text, 8 x 8, on the screen as seen (scrolled rows too) */
void view_text(int16_t sx, uint8_t y, const char *s, uint8_t colour)
{
    for (; *s; s++) {
        const uint8_t *g = glyph(*s);
        uint8_t c, r, m;
        for (c = 0, m = 0x80; c < 8; c++, m >>= 1, sx++) {
            uint8_t *p;
            if (sx < 0 || sx >= 320) continue;
            p = column(phys(sx, y)) + y;
            for (r = 0; r < 8; r++, p++)
                if (g[r] & m) *p = colour;
        }
    }
    columns_done();
}

void view_text_center(uint8_t y, const char *s, uint8_t colour)
{
    view_text(160 - (int16_t)strlen(s) * 4, y, s, colour);
}

/* ------------------------------------------------------------ the terrain (scrolled) */

/* TODO: switch to sdk l2scroll.h (one band, rows 20-236, world width 1920,
   the ridge as its fill, l2s_col for col_h) once it is on main with its
   world-width wrap (l2s_band_wrap) and zero-scroll rows outside the bands;
   see PROGRESS.md */

/* what each physical column shows: its world x (-1: nothing) and terrain
   row (0: none): col_wx, col_h (page 46) */
static int16_t shown_vx = -1;              /* the viewport the columns were drawn for */

/* the ridge: the surface row, then 6 rows (port: shaded down for 256
   colours): terrain.asm's tc_ridge */

/* terrain.asm: the columns tc_s0..tc_s1-1 of the view at tc_vx */
extern uint16_t tc_vx, tc_s0, tc_s1, tc_drawn;
extern uint8_t tc_flat;
extern WORD *tc_terrain;
extern void terrain_cols(void);

/* the columns entering the view (all of them after a jump) */
static void terrain_update(GameState *gs, int16_t vx)
{
    int16_t d, s0, s1;
    if (shown_vx < 0) { s0 = 0; s1 = 320; }
    else {
        d = vx - shown_vx;
        if (d > WORLD_W / 2) d -= WORLD_W;
        if (d < -WORLD_W / 2) d += WORLD_W;
        if (d >= 320 || d <= -320) { s0 = 0; s1 = 320; }
        else if (d > 0) { s0 = 320 - d; s1 = 320; }
        else { s0 = 0; s1 = -d; }
    }
    shown_vx = vx;
    tc_vx = (uint16_t)vx;
    tc_s0 = (uint16_t)s0;
    tc_s1 = (uint16_t)s1;
    tc_flat = gs->planet_destroyed ? 1 : 0;
    tc_terrain = gs->terrain_h;
    tc_drawn = 0;
    terrain_cols();
    view_cols_drawn += tc_drawn;
}

/* ------------------------------------------------------------ the scanner (rows 0-19) */

#define SCANNER_TOP_RANGE (SCANNER_H - 4)
static uint8_t scan_sy[160];               /* the terrain's top in each 2-pixel column, 0 = none */
static uint16_t dots_xy[96];               /* dynamic pixels drawn: x | y << 9 */
static uint8_t dots_n;

/* the original's scanner row of world row wy (a table: its 32-bit division is slow) */
static uint8_t scan_y(int16_t wy)
{
    return wy >= 0 && wy < 256 ? scan_ytab[wy] : 0;
}

/* ... and column of world pixel x: x * 320 / 1920 is x / 6 */
#define scan_x(wx) ((int16_t)((uint16_t)FROM_FP(wx) / 6))

static uint8_t scan_bg(uint16_t x, uint8_t y)
{
    uint8_t t = scan_sy[x >> 1];
    if (y == SCANNER_H - 1) return C_STAR_DIM;               /* the border, COL_SCANNER_BRD */
    if (y >= SCANNER_H) return C_BLACK;
    if (t && y >= t && y <= SCANNER_H - 2) return C_TERRAIN;
    return C_TERR_DK;                                         /* COL_SCANNER_BG */
}

static void scanner_static(GameState *gs)
{
    uint16_t x;
    uint8_t y;
    for (x = 0; x < 160; x++) {
        WORD ty = gs->terrain_h[(LONG)x * 2 * WORLD_W / SCREEN_W];
        WORD sy = (WORD)((LONG)(ty - PLAY_TOP) * (SCANNER_H - 4) / (PLAY_BOT - PLAY_TOP)) + 2;
        scan_sy[x] = (sy < 2 || sy >= SCANNER_H - 1) ? 0 : (uint8_t)sy;
    }
    for (x = 0; x < 320; x++) {
        uint8_t *c = column(x);
        for (y = 0; y < ROW_PLAY; y++) c[y] = scan_bg(x, y);
    }
    columns_done();
    dots_n = 0;
}

static void dot(int16_t x, uint8_t y, uint8_t colour)
{
    if (x < 0 || x >= SCREEN_W || y < 1 || y >= SCANNER_H - 1 || dots_n >= 96) return;
    column(x)[y] = colour;
    dots_xy[dots_n++] = (uint16_t)x | (uint16_t)y << 9;
}

static const uint8_t enemy_colour[7] = {C_WHITE, C_GREEN, C_MAGENTA, C_CYAN, C_RED, C_MAGENTA, C_ORANGE};

static void scanner_update(GameState *gs, int16_t vx)
{
    uint8_t i, y;
    int16_t left, right;
    while (dots_n) {                       /* last frame's dots off */
        uint16_t d = dots_xy[--dots_n];
        uint16_t x = d & 0x1FF;
        column(x)[d >> 9] = scan_bg(x, d >> 9);
    }
    /* the viewport's brackets */
    left = (int16_t)((uint16_t)vx / 6);
    right = left + (int16_t)((LONG)SCREEN_W * SCREEN_W / WORLD_W);   /* 53 (port: 16-bit int) */
    for (y = 1; y <= SCANNER_H - 2; y++) {
        dot(left % SCREEN_W, y, C_WHITE);
        dot(right % SCREEN_W, y, C_WHITE);
    }
    for (i = 0; i < MAX_HUMANS; i++) {
        Human *h = &gs->humans[i];
        if (h->state == HUMAN_DEAD) continue;
        dot(scan_x(h->wx), scan_y((int16_t)FROM_FP(h->wy)), C_WHITE);
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;
        dot(scan_x(e->wx), scan_y((int16_t)FROM_FP(e->wy)),
            enemy_colour[e->type < 7 ? e->type : 0]);
    }
    if (gs->ship.alive) {
        int16_t sx = scan_x(gs->ship.wx);
        uint8_t sy = scan_y((int16_t)FROM_FP(gs->ship.wy));
        dot(sx - 1, sy, C_WHITE);
        dot(sx, sy, C_WHITE);
        dot(sx + 1, sy, C_WHITE);
    }
    columns_done();
}

/* ------------------------------------------------------------ the HUD (rows 236-255) */

static LONG hud_score = -1, hud_hi = -1;
static WORD hud_lives = -1, hud_bombs = -1, hud_level = -1, hud_humans = -1;

static char *num(char *p, LONG v, uint8_t digits)
{
    char *e;
    uint8_t n = 0;
    char tmp[11];
    do { tmp[n++] = '0' + (char)(v % 10); v /= 10; } while (v && n < 10);
    while (n < digits) tmp[n++] = '0';
    e = p;
    while (n) *e++ = tmp[--n];
    *e = 0;
    return e;
}

static void hud_update(GameState *gs, uint8_t force)
{
    char buf[24], *p;
    uint8_t i;
    if (force || hud_score != gs->score) {
        hud_score = gs->score;
        view_fill(4, HUD_Y, 56, 8, C_BLACK);
        num(buf, gs->score, 7);
        view_text(4, HUD_Y, buf, C_CYAN);
    }
    if (force || hud_hi != gs->hiscore) {
        hud_hi = gs->hiscore;
        view_fill(80, HUD_Y, 80, 8, C_BLACK);
        strcpy(buf, "HI ");
        num(buf + 3, gs->hiscore, 7);
        view_text(80, HUD_Y, buf, C_CYAN);
    }
    if (force || hud_lives != gs->lives || hud_bombs != gs->ship.smart_bombs) {
        hud_lives = gs->lives;
        hud_bombs = gs->ship.smart_bombs;
        view_fill(200, HUD_Y, 104, 8, C_BLACK);
        for (i = 0; i < gs->lives && i < 5; i++) view_fill(200 + i * 10, HUD_Y + 1, 6, 5, C_YELLOW);
        for (i = 0; i < gs->ship.smart_bombs && i < 5; i++) view_fill(260 + i * 8, HUD_Y + 1, 5, 5, C_YELLOW);
    }
    if (force || hud_level != gs->level || hud_humans != gs->humans_alive) {
        hud_level = gs->level;
        hud_humans = gs->humans_alive;
        view_fill(4, HUD_Y + 9, 96, 8, C_BLACK);
        buf[0] = 'W';
        p = num(buf + 1, gs->level, 1);
        *p++ = ' ';
        *p++ = 'H';
        num(p, gs->humans_alive, 1);
        view_text(4, HUD_Y + 9, buf, C_CYAN);
    }
}

/* ------------------------------------------------------------ stars (the tilemap) */

#define NUM_STARS 25
static uint16_t tm_scroll_x;

static void stars_init(void)
{
    /* the original's stars (init_stars): its own generator, fixed positions */
    ULONG rng = 99999;
    uint8_t i, ntiles = 1;
    ZXN_WRITE_REG(0x68, 0x80);              /* the ULA off */
    ZXN_WRITE_REG(0x6B, 0xA0);              /* tilemap on, 40 x 32, no attribute bytes */
    ZXN_WRITE_REG(0x6C, 0x00);              /* every cell: palette offset 0 */
    ZXN_WRITE_REG(0x6E, 0x00);              /* map at 0x4000 */
    ZXN_WRITE_REG(0x6F, 0x05);              /* tiles at 0x4500 */
    ZXN_WRITE_REG(0x4C, 0x00);              /* pen 0 transparent */
    memset(TM_MAP, 0, 40 * 32);
    memset(TM_TILES, 0, 32);
    ZXN_WRITE_REG(0x43, 0x30);              /* the tilemap's first palette */
    ZXN_WRITE_REG(0x40, 1);
    ZXN_WRITE_REG(0x41, C_STAR_DIM);        /* COL_STAR_DIM */
    ZXN_WRITE_REG(0x41, 0x4B);              /* port: COL_STAR_BLUE had the same colour; a pale blue */
    for (i = 0; i < NUM_STARS; i++) {
        uint16_t wx, wy, x, y;
        uint8_t *cell, *t, pen;
        rng = rng * 1103515245UL + 12345UL;
        wx = (uint16_t)((rng >> 16) % WORLD_W);
        rng = rng * 1103515245UL + 12345UL;
        wy = (uint16_t)(PLAY_TOP + (rng >> 16) % (PLAY_BOT - PLAY_TOP - 20));
        pen = (rng & 1) ? 1 : 2;
        x = wx % SCREEN_W;                  /* the stars repeat every screen (draw_stars) */
        y = wy;
        cell = TM_MAP + (y >> 3) * 40 + (x >> 3);
        if (!*cell) {                       /* a tile of its own for each cell with stars */
            if (ntiles >= TM_MAX_TILES) continue;
            memset(TM_TILES + ntiles * 32, 0, 32);
            *cell = ntiles++;
        }
        t = TM_TILES + *cell * 32 + (y & 7) * 4 + ((x & 7) >> 1);
        *t |= (x & 1) ? pen : pen << 4;
    }
}

/* ------------------------------------------------------------ sprites */

/* coll.asm: world_to_screen_x(*wx, ws_vx) in 24 bits (the C one is 32-bit) */
extern LONG ws_vx;
extern int16_t world_sx(const LONG *wx) __z88dk_fastcall;

static uint8_t *ap;
static const char *msg_text;
static uint8_t msg_n[2];                    /* sprites in each message line */
static int16_t msg_x[2];

static void put(int16_t x, int16_t y, uint8_t pattern, uint8_t flags)
{
    if (ap >= attrs + ATTRS_LEN) return;        /* port: past 128 the rest is not shown */
    if (x <= -16 || x >= 320 || y <= -16 || y >= 256) return;
    ap[0] = (uint8_t)x;
    ap[1] = (uint8_t)y;
    ap[2] = flags | ((uint8_t)(x >> 8) & 1);
    ap[3] = 0x80 | pattern;                 /* visible, 4-byte form: 8 bpp */
    ap += 4;
}

static void sprites_update(GameState *gs)
{
    uint8_t i, k;
    ws_vx = gs->viewport_x;
    ap = attrs;
    /* the wave messages (on top) */
    for (k = 0; k < 2; k++)
        for (i = 0; i < msg_n[k]; i++)
            put(msg_x[k] + i * 16, k ? 125 : 100, P_TEXT + k * 8 + i, 0);
    /* explosions: four mirrored quarters around the centre */
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *e = &gs->explosions[i];
        int16_t sx, sy;
        uint8_t r, p;
        if (!e->active) continue;
        sx = world_sx(&e->wx);
        sy = (int16_t)FROM_FP(e->wy);
        r = (uint8_t)e->radius;
        if (sx < -r || sx > SCREEN_W + r) continue;
        p = P_EXPL(r < 2 ? 2 : r > 17 ? 17 : r);
        put(sx - 16, sy - 16, p, SPR_MIRROR_X | SPR_MIRROR_Y);
        put(sx, sy - 16, p, SPR_MIRROR_Y);
        put(sx - 16, sy, p, SPR_MIRROR_X);
        put(sx, sy, p, 0);
    }
    /* the ship (blinks while invulnerable) */
    {
        Ship *s = &gs->ship;
        if (s->alive && !(s->invuln_timer > 0 && (gs->frame & 3) < 2)) {
            int16_t sx = world_sx(&s->wx), sy = (int16_t)FROM_FP(s->wy);
            if (sx >= -20 && sx <= SCREEN_W + 20) {
                put(sx, sy, s->facing > 0 ? P_SHIP_R : P_SHIP_L, 0);
                if (s->thrust_on && (gs->frame & 1))
                    put(s->facing > 0 ? sx - 4 : sx + 17, sy, P_FLAME, s->facing > 0 ? 0 : SPR_MIRROR_X);
            }
        }
    }
    /* lasers, enemy shots, mines */
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        Bullet *b = &gs->player_bullets[i];
        if (b->active) put(world_sx(&b->wx), (int16_t)FROM_FP(b->wy), P_LASER, 0);
    }
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        Bullet *b = &gs->enemy_bullets[i];
        if (b->active) put(world_sx(&b->wx), (int16_t)FROM_FP(b->wy), P_EBULLET, 0);
    }
    for (i = 0; i < MAX_MINES; i++) {
        Mine *m = &gs->mines[i];
        if (m->active) put(world_sx(&m->wx), (int16_t)FROM_FP(m->wy), P_MINE, 0);
    }
    /* enemies */
    for (i = 0; i < MAX_ENEMIES; i++) {
        static const uint8_t pat[7] = {0, P_DIVER, P_STALKER, P_CHASER, P_DROPPER, P_HIVE, P_DRONE};
        Enemy *e = &gs->enemies[i];
        uint8_t p;
        if (!e->active || e->type < 1 || e->type > 6) continue;
        p = pat[e->type];
        if (p == P_DIVER && (e->anim_frame & 8)) p = P_DIVER_FLASH;
        put(world_sx(&e->wx), (int16_t)FROM_FP(e->wy), p, 0);
    }
    /* humans */
    for (i = 0; i < MAX_HUMANS; i++) {
        Human *h = &gs->humans[i];
        if (h->state != HUMAN_DEAD)
            put(world_sx(&h->wx), (int16_t)FROM_FP(h->wy), P_HUMAN, 0);
    }
    /* particles (screen coordinates; colour 12 of the original is black: not shown) */
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (p->life > 0 && p->x >= 0 && p->x < SCREEN_W && p->y >= PLAY_TOP && p->y < PLAY_BOT) {
            if (p->color == COL_EXPL_RED) put(p->x, p->y, P_PART_RED, 0);
            else if (p->color == COL_EXPL_RED + 1) put(p->x, p->y, P_PART_CYAN, 0);
        }
    }
    view_sprites_used = (uint8_t)((ap - attrs) >> 2);
    while (ap < attrs + ATTRS_LEN) { ap[3] = 0; ap += 4; }     /* the rest hidden */
}

/* sprite text: a line of the ROM font into patterns P_TEXT + line * 8 .. */
static void text_line(uint8_t line, const char *s, uint8_t colour)
{
    static uint8_t pat[256];
    uint8_t n = (uint8_t)strlen(s), i, c, r;
    if (n > 16) n = 16;
    msg_n[line] = (n + 1) >> 1;
    msg_x[line] = 160 - n * 4;
    for (i = 0; i < msg_n[line]; i++) {
        memset(pat, C_SKY, 256);
        for (c = 0; c < 2; c++) {
            uint8_t ch = 2 * i + c < n ? (uint8_t)s[2 * i + c] : ' ';
            const uint8_t *g = glyph((char)ch);
            for (r = 0; r < 8; r++) {
                uint8_t b = g[r], x;
                for (x = 0; x < 8; x++)
                    if (b & (0x80 >> x)) pat[r * 16 + c * 8 + x] = colour;
            }
        }
        spr_pattern(P_TEXT + line * 8 + i, pat);
    }
}

void view_message(WORD level, const char *msg)
{
    char buf[12];
    if (!msg) { msg_n[0] = msg_n[1] = 0; msg_text = 0; return; }
    if (msg == msg_text) return;
    msg_text = msg;
    strcpy(buf, "WAVE ");
    num(buf + 5, level, 1);
    text_line(0, buf, C_YELLOW);
    text_line(1, msg, C_WHITE);
    /* "WAVE n" is at x 120 in the original, the message centred */
    msg_x[0] = 120;
}

/* the game-over screen's prompt: the frozen frame's sprites stay, line 1 of
   the sprite text shows the prompt at the original's 96, 180 */
static const char *prompt_text;
static uint8_t prompt_on;

void view_prompt(const char *s)
{
    uint8_t *a;
    if (s && s != prompt_text) {
        text_line(1, s, C_YELLOW);
        prompt_text = s;
    }
    if ((s != 0) == prompt_on) return;
    prompt_on = s != 0;
    /* the last attribute slot: the prompt's sprites replace the end of the list */
    a = attrs + ATTRS_LEN - 4 * 8;
    {
        uint8_t i;
        for (i = 0; i < 8; i++, a += 4) {
            if (prompt_on && i < msg_n[1]) {
                int16_t x = 96 + i * 16;
                a[0] = (uint8_t)x;
                a[1] = 180;
                a[2] = (uint8_t)(x >> 8) & 1;
                a[3] = 0x80 | (P_TEXT + 8 + i);
            }
            else a[3] = 0;
        }
    }
}

/* ------------------------------------------------------------ the frame */

static uint8_t copper_ready;

void view_init(void)
{
    next_init();
    memcpy(font, (const void *)0x3D00, 96 * 8);
    ZXN_WRITE_REG(0x70, 0x10);              /* Layer 2 320 x 256, 8 bpp */
    ZXN_WRITE_REG(0x12, L2_BANK);
    ZXN_WRITE_REG(0x1C, 0x01);              /* its clip: everything (x in pixel pairs) */
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 159);
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 255);
    ZXN_WRITE_REG(0x16, 0);
    ZXN_WRITE_REG(0x17, 0);
    ZXN_WRITE_REG(0x71, 0);
    z80_outp(0x123B, 0x02);                 /* visible */
    ZXN_WRITE_REG(0x4A, 0x00);              /* the fallback colour (nothing in any layer): black */
    z80_outp(0xFE, 0);                      /* and the border */
    dma_fill_banks(L2_BANK, C_SKY, 5);
    spr_init();
    ZXN_WRITE_REG(0x15, ZXN_READ_REG(0x15) | 0x20);     /* clip sprites over the border too */
    ZXN_WRITE_REG(0x1C, 0x02);              /* sprites only in the playfield */
    ZXN_WRITE_REG(0x19, 0);
    ZXN_WRITE_REG(0x19, 159);
    ZXN_WRITE_REG(0x19, PLAY_TOP);
    ZXN_WRITE_REG(0x19, ROW_HUD - 1);
    spr_patterns_from_pages(SPR_PAGE, 1, 0, SPR_COUNT, 0xFF);
    {
        int16_t y;
        for (y = 0; y < 256; y++)
            scan_ytab[y] = (uint8_t)((LONG)(y - PLAY_TOP) * (SCANNER_H - 4) / (PLAY_BOT - PLAY_TOP) + 2);
    }
    stars_init();
    memset(attrs, 0, ATTRS_LEN);
}

static void clear_layer2(void)
{
    dma_fill_banks(L2_BANK, C_SKY, 5);
    memset(col_h, 0, 320);
    memset(col_wx, 0xFF, 640);
    shown_vx = -1;
}

void view_static_screen(GameState *gs)
{
    scrolled = 0;
    scroll = 0;
    clear_layer2();
    memset(attrs, 0, ATTRS_LEN);
    msg_n[0] = msg_n[1] = 0;
    msg_text = 0;
    prompt_text = 0;
    prompt_on = 0;
    tm_scroll_x = (uint16_t)(FROM_FP(gs->viewport_x) / 4) % 320;
    copper_ready = 1;
}

void view_play_screen(GameState *gs)
{
    clear_layer2();
    scrolled = 1;
    scanner_static(gs);
    view_fill(0, ROW_HUD - 1, 320, 256 - (ROW_HUD - 1), C_BLACK);
    hud_update(gs, 1);
}

void view_prepare(GameState *gs)
{
    int16_t vx = (int16_t)FROM_FP(gs->viewport_x);
    static uint8_t destroyed;
    view_cols_drawn = 0;
    if (gs->planet_destroyed != destroyed) {        /* the terrain goes: every column again */
        destroyed = (uint8_t)gs->planet_destroyed;
        shown_vx = -1;
    }
    terrain_update(gs, vx);
    scroll = (uint16_t)vx % 320;
    tm_scroll_x = (uint16_t)(vx / 4) % 320;
    scanner_update(gs, vx);
    hud_update(gs, 0);
    sprites_update(gs);
    copper_ready = 1;
}

void view_commit(void)
{
    /* the sprites */
    z80_outp(0x303B, 0);
    dma_to_port(0x57, attrs, ATTRS_LEN);
    /* the stars */
    ZXN_WRITE_REG(0x2F, (uint8_t)(tm_scroll_x >> 8));
    ZXN_WRITE_REG(0x30, (uint8_t)tm_scroll_x);
    /* the playfield's scroll, row by row */
    if (copper_ready) {
        copper_ready = 0;
        split_begin();
        copper_move(0x16, 0);
        copper_move(0x71, 0);
        if (scrolled) {
            split_row(ROW_PLAY);
            copper_move(0x16, (uint8_t)scroll);
            copper_move(0x71, (uint8_t)(scroll >> 8));
            split_row(ROW_HUD);
            copper_move(0x16, 0);
            copper_move(0x71, 0);
        }
        split_end();
    }
}
