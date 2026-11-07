/*
 * Dot Chase for the ZX Spectrum 48K - port of amiga_mcp/examples/dot_chase.
 *
 * game.c is the original logic (port changes marked); this is the Spectrum
 * front end, after the original's main.c and draw.c:
 *
 *  - the 28x31 maze in 6x6-pixel tiles (168x186: the original's 8x8 tiles
 *    would need 248 lines), the logic still in the original's 8-unit tiles
 *    and drawn at 3/4 scale, so speeds, timings and the AI are untouched
 *  - the maze is drawn once per level and kept in a shadow screen (SDK
 *    shadow48); each frame, right after the interrupt, the sprites' old
 *    places are restored from it and the sprites drawn again, top first,
 *    before the beam reaches them: no flicker, no tearing (a frame that ran
 *    long waits for the next interrupt rather than draw late)
 *  - colour from attributes: the hedge is green, each sprite colours the
 *    cells it covers (the Spectrum's colour clash, kept small by 6x6 sprites)
 *  - logic at a fixed 50 Hz; the picture is redrawn every frame
 *
 * Controls: Q/A/O/P, cursor keys, Sinclair 1/2, Kempston; fire starts.
 *
 * Memory: 0x4000 screen, 0x6000-0x7AFF shadow screen (bitmap + attributes),
 * 0x7B00-0x7F37 the game state (game.h, fixed.asm), 0x8000 the program
 * (below 0xF000), 0xF000 the SDK's IM 2 table, the stack under 0xFF58.
 */
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <arch/zx.h>
#include "game.h"
#include "art.h"
#include "sound.h"
#include "zxgame.h"
#include "shadow48.h"
#include "zxlog.h"
#include "zxtext.h"

/* ------------------------------------------------------------ layout */

#define MX 8                          /* maze top-left on the screen */
#define MY 3
#define TILE_PX 6
#define HUD_COL 23                    /* the HUD: columns 23..31 */

#define A_MAZE   (PAPER_BLACK | INK_GREEN | BRIGHT)
#define A_LABEL  (PAPER_BLACK | INK_GREEN)
#define A_VALUE  (PAPER_BLACK | INK_WHITE | BRIGHT)
#define A_BANNER (PAPER_BLACK | INK_YELLOW | BRIGHT)
#define A_HERO   (PAPER_BLACK | INK_RED | BRIGHT)
#define A_FRIGHT (PAPER_BLACK | INK_BLUE | BRIGHT)
#define A_WHITE  (PAPER_BLACK | INK_WHITE | BRIGHT)

/* the original's spider colours: purple, steel, brown, bone */
static const uint8_t spider_attr[NUM_SPIDERS] = {
    PAPER_BLACK | INK_MAGENTA | BRIGHT, PAPER_BLACK | INK_CYAN | BRIGHT,
    PAPER_BLACK | INK_YELLOW, PAPER_BLACK | INK_WHITE | BRIGHT,
};
static const uint8_t bonus_attr[MAX_BONUS_TYPE] = {
    PAPER_BLACK | INK_MAGENTA | BRIGHT,       /* berry */
    PAPER_BLACK | INK_RED | BRIGHT,           /* mushroom */
    PAPER_BLACK | INK_RED,                    /* carrot */
    PAPER_BLACK | INK_GREEN | BRIGHT,         /* pear */
    PAPER_BLACK | INK_YELLOW,                 /* acorn */
    PAPER_BLACK | INK_YELLOW | BRIGHT,        /* honey */
    PAPER_BLACK | INK_YELLOW | BRIGHT,        /* sunflower */
};

/* world units (8 a tile) -> screen pixels (6 a tile), rounding down:
   w2s(p) = floor(p * 3 / 4); scr_tab[p + 8] = MX + w2s(p) for p = -8..247
   (the tunnel runs from -8 to 224), MY + w2s(p) is that - (MX - MY) */
static uint8_t scr_tab[256];

static void scr_tab_init(void)
{
    uint8_t i = 0;
    do {
        int16_t p = (int16_t)i - 8;
        scr_tab[i] = MX + (p >= 0 ? (p * 3) >> 2 : -((-p * 3 + 3) >> 2));
    } while (++i);
}

#define SX(p) scr_tab[(uint8_t)((p) + 8)]
#define SY(p) (scr_tab[(uint8_t)((p) + 8)] - (MX - MY))

/* ------------------------------------------------------------ the maze */

/* hedge: open around the maze, so the outer edge gets a line too */
#define hedge(x, y) ((uint8_t)(x) < MAZE_W && (uint8_t)(y) < MAZE_H && maze_row[y][x] == T_WALL)

/* port: the original fills hedge tiles with leafy green; filled 6x6 blocks
   would turn the colour of every sprite passing by (the cells' ink is the
   sprite's), so the hedge is drawn as its edge where it meets the paths,
   rounded at the corners, with leafy speckles inside thick hedge (placed by
   the original's (tx * 7 + ty * 13) & 3) */
static img6 hedge_img;

static const uint8_t *hedge_tile(int8_t tx, int8_t ty)
{
    uint8_t n = !hedge(tx, ty - 1), s = !hedge(tx, ty + 1);
    uint8_t w = !hedge(tx - 1, ty), e = !hedge(tx + 1, ty);
    uint8_t h = (tx * 7 + ty * 13) & 3;
    uint8_t side = (w ? 0x80 : 0) | (e ? 0x04 : 0);
    uint8_t r0 = side, r1 = side, r2 = side, r3 = side, r4 = side, r5 = side, y;
    uint8_t *p = hedge_img;
    if (n) r0 = 0xFC;
    if (s) r5 = 0xFC;
    if (!n && !w && !hedge(tx - 1, ty - 1)) r0 = r0 | 0x80;   /* inside corners */
    if (!n && !e && !hedge(tx + 1, ty - 1)) r0 = r0 | 0x04;
    if (!s && !w && !hedge(tx - 1, ty + 1)) r5 = r5 | 0x80;
    if (!s && !e && !hedge(tx + 1, ty + 1)) r5 = r5 | 0x04;
    if (!(n | s | w | e)) {                                    /* leaves inside */
        if (h & 1) r2 = r2 | (0x40 >> h); else r3 = r3 | (0x20 >> h);
        if (h & 2) r1 = r1 | 0x10; else r4 = r4 | 0x08;
    }
    *p++ = 6 | 0x80;
    for (y = 0; y < 6; y++)
        *p++ = y == 0 ? r0 : y == 1 ? r1 : y == 2 ? r2 : y == 3 ? r3 : y == 4 ? r4 : r5;
    return hedge_img;
}

static const uint8_t *tile_img(uint8_t tx, uint8_t ty)
{
    switch (maze_row[ty][tx]) {
    case T_WALL:        return hedge_tile(tx, ty);
    case T_DOT:         return img_seed;
    case T_POWER:       return img_flower[(game.frame >> 3) & 1 ? 0 : 1];
    case T_SPIDER_DOOR: return img_gate;
    }
    return img_empty;
}

#define TILE_XY(tx, ty) SPR8_XY(MX + (tx) * TILE_PX, MY + (ty) * TILE_PX)

/* tiles changed in the shadow, shown on the screen before the sprites */
#define MAX_DIRTY 8
static uint16_t dirty_xy[MAX_DIRTY];
static uint8_t n_dirty;

static void tile_update(uint8_t tx, uint8_t ty)
{
    uint16_t xy = TILE_XY(tx, ty);
    spr8_plane = SHADOW_PLANE;
    spr8_draw(xy, tile_img(tx, ty));
    spr8_plane = SHADOW_SCREEN;
    if (n_dirty < MAX_DIRTY) dirty_xy[n_dirty++] = xy;
}

/* the maze, drawn on the screen (it appears row by row in under a
   second), then copied to the shadow */
static void maze_draw(void)
{
    uint8_t tx, ty;
    for (ty = 0; ty < MAZE_H; ty++) {
        const uint8_t *row = maze_row[ty];
        for (tx = 0; tx < MAZE_W; tx++)
            if (row[tx] != T_EMPTY) spr8_draw(TILE_XY(tx, ty), tile_img(tx, ty));
    }
    shadow_save();
}

/* the flowers pulse between bright and dim every 8 frames */
static uint8_t flower_phase;

static void flowers(void)
{
    static const uint8_t at[4][2] = { {1, 5}, {26, 5}, {1, 21}, {26, 21} };
    uint8_t i, ph = (game.frame >> 3) & 1;
    if (ph == flower_phase) return;
    flower_phase = ph;
    for (i = 0; i < 4; i++)
        if (maze_row[at[i][1]][at[i][0]] == T_POWER) tile_update(at[i][0], at[i][1]);
}

/* ------------------------------------------------------------ banner text ("GO BUG GO") */

#define BANNER_ROW 13                 /* character row: lines 104..111 (tile rows 16..18) */
#define BANNER_C0 5
#define BANNER_W 13
static uint8_t banner_shown;          /* 0, 1 = GO BUG GO, 2 = GARDEN CLEAR */
static uint8_t band_dirty;

static uint8_t banner_under[8 * BANNER_W];   /* the shadow under the banner */

static void banner(uint8_t which)
{
    uint8_t i, *u = banner_under;
    if (which == banner_shown) return;
    for (i = 0; i < 8; i++, u += BANNER_W) {
        uint8_t *sh = zx_row[BANNER_ROW * 8 + i] + BANNER_C0 + 0x2000;
        if (!banner_shown) memcpy(u, sh, BANNER_W);      /* keep the maze */
        if (which) memset(sh, 0, BANNER_W);
        else memcpy(sh, u, BANNER_W);                    /* the maze again */
    }
    banner_shown = which;
    if (which) {
        const char *s = which == 1 ? "GO BUG GO" : "GARDEN CLEAR";
        uint8_t c = BANNER_C0 + (BANNER_W - strlen(s) + 1) / 2;
        for (; *s; s++, c++) {
            const uint8_t *g = (const uint8_t *)0x3C00 + (uint16_t)(uint8_t)*s * 8;
            for (i = 0; i < 8; i++) zx_row[BANNER_ROW * 8 + i][c + 0x2000] = g[i];
        }
    }
    memset(SHADOW_ATTR + BANNER_ROW * 32 + BANNER_C0, which ? A_BANNER : A_MAZE, BANNER_W);
    band_dirty = 1;
}

/* ------------------------------------------------------------ sprites */

typedef spr8_obj Obj;                 /* w: pixels wide (6, a popup 7) */

enum { O_BONUS, O_SPIDER, O_HERO = O_SPIDER + NUM_SPIDERS, O_POP1, O_POP2, NOBJ };
static Obj obj[NOBJ], drawn[NOBJ];        /* this frame's, the last drawn */

/* right after the interrupt: changed tiles, then the sprites' old places
   (pixels and colours) back from the shadow and the sprites in their new
   places and colours; the cells a sprite covers take its colour */
static void render(void)
{
    uint8_t i;
    for (i = 0; i < n_dirty; i++) spr8_restore(dirty_xy[i], TILE_PX);
    n_dirty = 0;
    if (band_dirty) {
        shadow_show_rect(BANNER_C0, BANNER_ROW * 8, BANNER_W, 8);
        memcpy((uint8_t *)0x5800 + BANNER_ROW * 32 + BANNER_C0,
               SHADOW_ATTR + BANNER_ROW * 32 + BANNER_C0, BANNER_W);
        band_dirty = 0;
    }
    spr8_frame(drawn, obj, NOBJ);
    memcpy(drawn, obj, sizeof(obj));
}

/* a sprite at an actor's place (6x6). prepare() names the game state and
   the objects with constant indices: zsdcc then uses fixed addresses, where
   pointers cost it several times as much */
#define PLACE(o, b, im, a) do { \
        (o).on = 1; (o).w = TILE_PX; \
        (o).x = SX((b).pixel_x); (o).y = SY((b).pixel_y); \
        (o).img = (im); (o).attr = (a); } while (0)

#define SPIDER(i) do { \
        Spider *g = &game.spiders[i]; \
        uint8_t d = g->dir < 0 ? DIR_LEFT : (uint8_t)g->dir; \
        if (g->state == SPIDER_EATEN) { \
            PLACE(obj[O_SPIDER + i], g->b, husk_img[d], A_WHITE); \
        } else { \
            uint8_t a = spider_attr[i]; \
            if (g->state == SPIDER_FRIGHT) \
                a = g->fright_timer < FRIGHT_BLINK && (g->fright_timer & 8) ? A_WHITE : A_FRIGHT; \
            PLACE(obj[O_SPIDER + i], g->b, spider_img[(d << 1) | leg], a); \
        } } while (0)

/* the sprites for the game state, as the original's draw_game() */
static void prepare(void)
{
    uint8_t leg = (game.frame >> 2) & 1;
    int8_t dir = game.hero.dir < 0 ? DIR_LEFT : game.hero.dir;

    if (game.bonus_active) {
        uint8_t t = game.bonus_type;
        obj[O_BONUS].on = 1;
        obj[O_BONUS].w = TILE_PX;
        obj[O_BONUS].x = SX(game.bonus_x);
        obj[O_BONUS].y = SY(game.bonus_y);
        obj[O_BONUS].img = img_bonus[t];
        obj[O_BONUS].attr = bonus_attr[t];
    } else {
        obj[O_BONUS].on = 0;
    }

    SPIDER(0);
    SPIDER(1);
    SPIDER(2);
    SPIDER(3);

    if (game.state == STATE_DYING) {
        /* the ladybug flips over and curls up (the original's draw_dying) */
        uint8_t phase = (DYING_TIME - game.state_timer) >> 3;
        if (phase < 3)
            PLACE(obj[O_HERO], game.hero.b, bug_img[(((dir + phase) & 3) << 1) | (phase & 1)], A_HERO);
        else if (phase < 7)
            PLACE(obj[O_HERO], game.hero.b, img_shrink[phase - 3], phase & 1 ? A_WHITE : A_HERO);
        else
            obj[O_HERO].on = 0;
    } else {
        PLACE(obj[O_HERO], game.hero.b, bug_img[(dir << 1) | game.hero.leg_step], A_HERO);
    }

    if (game.popup_active) {
        static int32_t pop_score = -1;
        uint8_t x = SX(game.popup_x) + 3 - 7, y = SY(game.popup_y);
        if (game.popup_score != pop_score) {
            pop_score = game.popup_score;
            art_popup((uint16_t)pop_score);
        }
        obj[O_POP1].on = obj[O_POP2].on = 1;
        obj[O_POP1].w = obj[O_POP2].w = 7;
        obj[O_POP1].x = x;
        obj[O_POP2].x = x + 8;
        obj[O_POP1].y = obj[O_POP2].y = y;
        obj[O_POP1].img = img_popup[0];
        obj[O_POP2].img = img_popup[1];
        obj[O_POP1].attr = obj[O_POP2].attr = A_BANNER;
    } else {
        obj[O_POP1].on = obj[O_POP2].on = 0;
    }
}

/* ------------------------------------------------------------ HUD (right of the maze) */

static char hud_digits[8] = "0000000";
static int32_t hud_score;
static int16_t hud_lives = -1, hud_level = -1;
static int32_t hud_best = -1;

/* add to the shown score in decimal (cheaper than 32-bit divisions per
   seed); returns the leftmost digit that changed */
static uint8_t hud_add(int32_t d)
{
    uint8_t first = 7;
    while (d > 0) {
        uint16_t step = d >= 1000 ? 1000 : d >= 100 ? 100 : d >= 10 ? 10 : 1;
        int8_t i = step == 1000 ? 3 : step == 100 ? 4 : step == 10 ? 5 : 6;
        d -= step;
        while (i >= 0) {
            if (i < first) first = i;
            if (hud_digits[i] == '9') { hud_digits[i] = '0'; i--; }
            else { hud_digits[i]++; break; }
        }
    }
    return first;
}

static void hud_labels(void)
{
    attr_fill(HUD_COL, 0, 9, 24, A_LABEL);
    zx_print_at(HUD_COL + 1, 1, "SCORE");
    zx_print_at(HUD_COL + 1, 5, "BEST");
    zx_print_at(HUD_COL + 1, 9, "BUGS");
    zx_print_at(HUD_COL + 1, 15, "GARDEN");
    attr_fill(HUD_COL + 1, 2, 7, 1, A_VALUE);
    attr_fill(HUD_COL + 1, 6, 7, 1, A_VALUE);
    attr_fill(HUD_COL + 1, 10, 7, 2, A_HERO);
    attr_fill(HUD_COL + 1, 16, 7, 1, A_VALUE);
    hud_lives = hud_level = -1;
    hud_best = -1;
    hud_score = -1;
}

static void hud(void)
{
    if (game.score != hud_score) {
        uint8_t first = 0;
        if (game.score < hud_score || hud_score < 0) {
            uint8_t i;
            for (i = 0; i < 7; i++) hud_digits[i] = '0';
            hud_score = 0;
            hud_add(game.score);
        } else {
            first = hud_add(game.score - hud_score);
        }
        hud_score = game.score;
        zx_print_at(HUD_COL + 1 + first, 2, hud_digits + first);
    }
    if (game.hiscore != hud_best) {
        text_num(HUD_COL + 1, 6, game.hiscore, 7);
        hud_best = game.hiscore;
    }
    if (game.lives != hud_lives) {
        /* spare ladybugs, as the original's draw_hud_lives */
        uint8_t i;
        gfx_clear_rect(HUD_COL + 1, 80, 7, 16);
        for (i = 0; i < game.lives - 1 && i < 6; i++)
            spr8_draw(SPR8_XY((HUD_COL + 1 + (i % 3) * 2) * 8 + 1, 81 + (i / 3) * 8), img_bug[DIR_UP][0]);
        hud_lives = game.lives;
    }
    if (game.level != hud_level) {
        uint8_t b = (game.level - 1) % MAX_BONUS_TYPE;
        text_num(HUD_COL + 1, 16, game.level, 3);
        gfx_clear_rect(HUD_COL + 5, 128, 1, 8);
        spr8_draw(SPR8_XY((HUD_COL + 5) * 8 + 1, 129), img_bonus[b]);
        attr_set(HUD_COL + 5, 16, bonus_attr[b]);
        hud_level = game.level;
    }
}

/* a new level: the whole maze again */
static void game_screen(void)
{

    screen_clear(A_MAZE, INK_BLACK);
    maze_draw();
    hud_labels();
    hud();
    memset(obj, 0, sizeof(obj));
    memset(drawn, 0, sizeof(drawn));
    n_dirty = 0;
    banner_shown = 0;
    band_dirty = 0;
    flower_phase = (game.frame >> 3) & 1;
}

/* ------------------------------------------------------------ title and game over */

static const char *const spider_names[NUM_SPIDERS] = { "WEBSTER", "SILK", "TANGLE", "NIBS" };
static const char *const spider_habits[NUM_SPIDERS] = {
    "HUNTS YOU", "CUTS YOU OFF", "TEAMS UP", "LOSES INTEREST"
};

static uint16_t title_t;
static uint8_t title_page;
static int16_t title_bug_x;

#define TITLE_SEED_Y 50

static void title_seeds(void)
{
    uint8_t x;
    spr8_plane = SHADOW_PLANE;
    for (x = 20; x < 240; x += 14) spr8_draw(SPR8_XY(x, TITLE_SEED_Y), img_seed);
    spr8_plane = SHADOW_SCREEN;
    shadow_show_rect(0, TITLE_SEED_Y, 32, 6);
}

static void title_page_draw(void)
{
    uint8_t i;
    for (i = 8 * 8; i < 20 * 8; i++) memset(zx_row[i], 0, 32);
    attr_fill(0, 8, 32, 12, A_VALUE);
    if (title_page == 0) {
        zx_print_at(10, 8, "THE SPIDERS");
        for (i = 0; i < NUM_SPIDERS; i++) {
            uint8_t row = 10 + i * 2;
            spr8_draw(SPR8_XY(17, row * 8 + 1), img_spider[DIR_RIGHT][0]);
            attr_set(2, row, spider_attr[i]);
            zx_print_at(4, row, spider_names[i]);
            attr_fill(4, row, 7, 1, spider_attr[i]);
            zx_print_at(13, row, spider_habits[i]);
        }
        spr8_draw(SPR8_XY(57, 18 * 8 + 1), img_seed);
        zx_print_at(9, 18, "10 PTS");
        spr8_draw(SPR8_XY(57, 19 * 8 + 1), img_flower[1]);
        zx_print_at(9, 19, "50 PTS  SCARES SPIDERS");
        attr_fill(7, 18, 1, 1, A_MAZE);
        attr_set(7, 19, A_MAZE);
    } else {
        attr_fill(0, 9, 32, 1, A_BANNER);
        text_center(9, "HIGH SCORES");
        hs_draw(10, 12);
    }
}

static void title_screen(void)
{
    screen_clear(A_VALUE, INK_BLACK);
    attr_fill(0, 1, 32, 2, A_HERO);
    text_big(7, 1, "DOT CHASE");
    attr_fill(0, 4, 32, 1, A_MAZE);
    text_center(4, "A LADYBUG IN THE HEDGE MAZE");
    gfx_fill_rect(0, 58, 32, 2, 0xFF);        /* the hedge under the seeds */
    attr_fill(0, 6, 32, 2, A_MAZE);
    attr_fill(0, 21, 32, 1, A_LABEL);
    text_center(21, "QAOP/CURSORS/KEMPSTON/SINCLAIR");
    title_t = 0;
    title_page = 0;
    title_page_draw();
    shadow_save();
    title_seeds();
    title_bug_x = -20;
}

static void title_frame(void)
{
    /* the ladybug trundles along the row of seeds, eating them */
    static uint8_t bug_on, bug_x;
    int16_t x = title_bug_x;
    if (bug_on) {
        spr8_restore(SPR8_XY(bug_x, TITLE_SEED_Y), 6);
        spr8_attr_restore(SPR8_XY(bug_x, TITLE_SEED_Y), 6 | 6 << 8);
    }
    bug_on = x >= 0 && x <= 240;
    if (bug_on) {
        bug_x = (uint8_t)x;
        if (((uint8_t)(x - 20 + 2) % 14) == 0 && x >= 18) {   /* a seed under its head */
            spr8_plane = SHADOW_PLANE;
            spr8_draw(SPR8_XY(x + 2, TITLE_SEED_Y), img_empty);
            spr8_plane = SHADOW_SCREEN;
        }
        spr8_draw(SPR8_XY(bug_x, TITLE_SEED_Y), img_bug[DIR_RIGHT][(title_t >> 2) & 1]);
        spr8_attr_fill(SPR8_XY(bug_x, TITLE_SEED_Y), 6 | 6 << 8, A_HERO);
    }
    title_t++;
    title_bug_x += 1;
    if (title_bug_x > 275) { title_bug_x = -20; title_seeds(); }
    if (title_t % 400 == 0) {
        title_page ^= 1;
        title_page_draw();
    }
    if ((title_t & 7) == 0) {
        attr_fill(0, 23, 32, 1, title_t & 8 ? A_VALUE : PAPER_BLACK | INK_BLACK);
        if ((title_t & 15) == 8) text_center(23, "PRESS FIRE TO START");
    }
}

static char num_buf[11];

static const char *num_str(int32_t v, uint8_t digits)
{
    uint8_t i = digits;
    num_buf[i] = 0;
    while (i--) {
        num_buf[i] = v || i == digits - 1 ? '0' + (uint8_t)(v % 10) : ' ';
        v /= 10;
    }
    return num_buf;
}

static void gameover_screen(void)
{
    screen_clear(A_VALUE, INK_BLACK);
    attr_fill(0, 4, 32, 2, A_HERO);
    text_big(7, 4, "GAME OVER");
    text_center(9, "SCORE");
    attr_fill(0, 11, 32, 2, A_BANNER);
    text_big(9, 11, num_str(game.score, 7));
    attr_fill(0, 15, 32, 1, A_LABEL);
    text_center(15, "GARDEN");
    text_center(16, num_str(game.level, 3));
    if (game.score >= game.hiscore) {
        attr_fill(0, 18, 32, 1, A_MAZE);
        text_center(18, "NEW BEST SCORE");
    }
}

static void gameover_frame(void)
{
    if ((game.title_blink & 7) == 0) {
        attr_fill(0, 21, 32, 1, game.title_blink & 8 ? A_VALUE : PAPER_BLACK | INK_BLACK);
        if ((game.title_blink & 15) == 8) text_center(21, "PRESS FIRE");
    }
}

/* ------------------------------------------------------------ test probe + log */

/* for test bots: where test.py finds things in the game state (at 0x7B00) */
const uint16_t dc_layout[] = {
    offsetof(GameState, hero), offsetof(GameState, spiders), sizeof(Spider),
    offsetof(Spider, state), offsetof(Spider, home), offsetof(GameState, state),
    offsetof(GameState, fright_active), offsetof(GameState, fright_timer),
    offsetof(Hero, dir), offsetof(GameState, lives),
};

static void log_state(void)
{
    static int16_t last_state = -1, last_lives = -1, last_level = -1;
    static const char *const names[7] = {
        "title", "ready", "playing", "dying", "gameover", "levelclear", "eatspider"
    };
    if (game.state != last_state) {
        zx_log("dot: state=%s score=%ld lives=%d level=%d\n", names[game.state], game.score,
               game.lives, game.level);
        last_state = game.state;
    }
    /* (no line per seed: a zx_log costs ~15,000 T-states of printf) */
    if (game.lives != last_lives) { zx_log("dot: lives=%d\n", game.lives); last_lives = game.lives; }
    if (game.level != last_level) { zx_log("dot: level=%d\n", game.level); last_level = game.level; }
}

/* ------------------------------------------------------------ events (the original main loop's) */

static uint8_t events(void)
{
    uint8_t ev = game.ev_flags, redraw = 0;
    game.ev_flags = 0;
    if (ev & EV_LEVEL) redraw = 1;
    else if (ev & (EV_SEED | EV_POWER)) tile_update(game.eat_x, game.eat_y);
    if (ev & EV_SEED)       snd_seed();
    if (ev & EV_EAT_SPIDER) { snd_eat_spider(); zx_log("dot: ate spider score=%ld\n", game.popup_score); }
    if (ev & EV_DIE)        { snd_die(); zx_log("dot: death x=%d y=%d\n", game.hero.b.tile_x, game.hero.b.tile_y); }
    if (ev & EV_BONUS)      { snd_bonus(); zx_log("dot: bonus score=%ld\n", game.popup_score); }
    if (ev & EV_EXTRA)      { snd_extra_life(); zx_log("dot: extra life\n"); }
    if (ev & EV_POWER)      { snd_power(); zx_log("dot: flower\n"); }

    /* crickets and the scared drone */
    {
        static uint8_t prev_fright;
        uint8_t fright_now = game.fright_active ? 1 : 0;
        if (prev_fright && !fright_now) snd_power_off();
        prev_fright = fright_now;
    }
    if (game.state == STATE_PLAYING) {
        if (game.fright_active) snd_pulse_off();
        else snd_set_pulse((uint8_t)game.pulse_speed);
    } else {
        snd_pulse_off();
        snd_power_off();
    }
    snd_tick();
    return redraw;
}

/* ------------------------------------------------------------ main loop */

enum { SCR_TITLE, SCR_GAME, SCR_OVER };

static void isr_work(void)
{
    input_sample();
}

/* frames-per-second accounting, logged every 500 frames of play */
static uint16_t perf_frames, perf_draws;
static uint8_t perf_max_ticks;

int main(void)
{
    uint8_t screen = SCR_TITLE;

    gfx_init();
    art_init();
    scr_tab_init();
    input_init();
    input_fast();                     /* the table decode: ~9,000 T-states less a read */
    ay_init();
    hs_init(2000);                    /* 10000 8000 6000 4000 2000: the original's best is 10000 */
    frame_init();
    input_latch(1);
    frame_hook = isr_work;
    game_init(&game);
    title_screen();
    zx_log("dot: ready kempston=%u ay=%u\n", input_kempston, ay_present);
    log_state();

    for (;;) {
        uint8_t ticks, t, b, p, redraw = 0;
        int16_t prev_state = game.state;
        InputState in;

        ticks = frame_wait();
        /* drawing must start in the top border, ahead of the beam: after a
           frame that ran long, wait for the next one (25 fps, no tearing) */
        if (ticks > 1) ticks += frame_wait();
        if (ticks > 3) ticks = 3;

        if (screen == SCR_GAME) {
            render();
            hud();
            perf_draws++;
            perf_frames += ticks;
            if (ticks > perf_max_ticks) perf_max_ticks = ticks;
            if (perf_frames >= 500) {
                zx_log("dot: perf frames=%u drawn=%u maxticks=%u score=%ld\n", perf_frames, perf_draws,
                       perf_max_ticks, game.score);
                perf_frames = perf_draws = 0;
                perf_max_ticks = 0;
            }
        } else if (screen == SCR_TITLE) {
            title_frame();
        } else {
            gameover_frame();
        }
        snd_frame();

        /* the original's input: the last of up, down, left, right held */
        b = input_read();
        p = input_pressed();
        in.dir = DIR_NONE;
        if (b & IN_UP)    in.dir = DIR_UP;
        if (b & IN_DOWN)  in.dir = DIR_DOWN;
        if (b & IN_LEFT)  in.dir = DIR_LEFT;
        if (b & IN_RIGHT) in.dir = DIR_RIGHT;
        in.start = (p & (IN_FIRE | IN_START)) != 0;
        in.quit = 0;

        if (game.state == STATE_TITLE && in.start) {
            game.hiscore = hs_table[0].score;   /* port: the best from the table */
        }

        for (t = 0; t < ticks; t++) {
            game_update(&game, &in);
            in.start = 0;                     /* a press counts once */
            redraw |= events();
            log_state();
            if (redraw || game.state != prev_state && (game.state == STATE_GAMEOVER || game.state == STATE_TITLE))
                break;
        }

        if (redraw) {
            game_screen();
            screen = SCR_GAME;
        }
        if (game.state != prev_state) {
            if (game.state == STATE_GAMEOVER) {
                snd_stop();
                gameover_screen();
                screen = SCR_OVER;
                zx_log("dot: gameover score=%ld level=%d\n", game.score, game.level);
            } else if (game.state == STATE_TITLE) {
                int8_t place = hs_rank(game.score);
                if (place >= 0) hs_enter(place, game.score, "dot");
                title_screen();
                screen = SCR_TITLE;
                zx_log("dot: title\n");
            } else if (game.state == STATE_LEVEL_DONE) {
                zx_log("dot: level clear level=%d\n", game.level);
            }
        }
        if (screen == SCR_GAME) {
            banner(game.state == STATE_READY ? 1 :
                   game.state == STATE_LEVEL_DONE && ((game.state_timer >> 3) & 1) ? 2 : 0);
            flowers();
            prepare();
        }
    }
}
