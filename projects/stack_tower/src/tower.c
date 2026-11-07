/*
 * tower.c - the tower in 3D, as the original's render.c draw_tower() shows
 * it, from pictures drawn on the host with its own camera and faces
 * (tools/gen_gfx.py):
 *
 *   1. the base picture for the view's step within a column (backdrop,
 *      core, grid, red line): four 8K pages copied by DMA
 *   2. the blocks: each a brick image for its position round the tower
 *      and its row, the furthest round first and, in each column, the
 *      rows towards the eye's height last (painter's order: what is in
 *      front is drawn later, so the faces a neighbour hides are covered)
 *   3. the drop guide on the core: the falling piece's columns, from the
 *      piece down to where it lands, made by the copper (guide_events)
 *   4. the ghost, then the falling piece
 *
 * 1 and 2 change only when the view turns or the board changes: they are
 * kept in four more pages and copied back by DMA in the other frames.
 *
 * port: the original sorts every face by depth each frame; here the order
 * comes from the tower's shape. The view turns in eighths of a column.
 */
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "l2wrun.h"
#include "nextcopper.h"
#include "nextsplit.h"
#include "gfx_data.h"
#include "stack.h"

uint16_t tower_bricks, tower_redraws;

/* the row at the eye's height (the camera is at 300, rows 26 high): the rows
   above it are drawn from the top down, then the rest from the bottom up */
#define EYE_ROW 11
#define CORE_RGB  0x04A                 /* the core (0x334) and the guide (0x557) as RGB333 */
#define GUIDE_RGB 0x093
static const uint8_t empty_row[ST_COLS];

static uint8_t vstep;           /* the view in eighths of a column, 0..127 */

/* a column's position round the tower in eighths, -64..63 (0: facing you) */
static int8_t rel_of(uint8_t col)
{
    return (int8_t)(((col << 3) - vstep + 64) & 127) - 64;
}

/* a brick image: lit faces in pen base, shaded ones in base + 1 */
static void brick(uint8_t set, int8_t rel, uint8_t row, uint8_t base)
{
    uint8_t a = rel < 0 ? -rel : rel, i, page;
    uint16_t off;
    if (a >= GFX_NANG || row >= ST_TOTAL) return;
    i = set * GFX_NANG + a;
    page = brick_page[i];
    ZXN_WRITE_REG(0x50, page);
    off = ((const uint16_t *)brick_addr[i])[row];
    if (off >= 0x2000) ZXN_WRITE_REG(0x50, page + (off >> 13));
    l2wr_blit((const uint8_t *)(off & 0x1FFF), rel < 0 ? GFX_VIEW_MIRROR : 0, rel < 0, base);
    tower_bricks++;
}

/* a board brick in a column whose block (page, table) is known */
static void column_brick(uint8_t page, const uint16_t *table, uint8_t row, int16_t x, uint8_t mirror, uint8_t base)
{
    uint16_t off;
    ZXN_WRITE_REG(0x50, page);
    off = table[row];
    if (off >= 0x2000) ZXN_WRITE_REG(0x50, page + (off >> 13));
    l2wr_blit((const uint8_t *)(off & 0x1FFF), x, mirror, base);
    tower_bricks++;
}

static void piece_bricks(uint8_t set, const Piece *p, uint8_t base)
{
    int8_t cells[5][2];
    uint8_t n = st_cells_of(p, cells), k, order[5], m = 0, j;
    /* the furthest round first */
    for (k = 0; k < n; k++) {
        int8_t r = rel_of(cells[k][0]);
        uint8_t a = r < 0 ? -r : r;
        for (j = m; j > 0; j--) {
            int8_t q = rel_of(cells[order[j - 1]][0]);
            if ((q < 0 ? -q : q) >= a) break;
            order[j] = order[j - 1];
        }
        order[j] = k;
        m++;
    }
    for (k = 0; k < n; k++) {
        j = order[k];
        if (cells[j][1] >= 0) brick(set, rel_of(cells[j][0]), (uint8_t)cells[j][1], base);
    }
}

/* the base picture and the board's blocks for the view's step, into the
   back buffer */
static void draw_board(void)
{
    uint8_t cols[ST_COLS], ncols = 0, c, k, j, top;
    const StGame *g = &game;

    dma_copy_pages(l2w_bank * 2 + GFX_BASE_X0 / 32, GFX_BASE_PAGE + 4 * (vstep & (GFX_STEPS - 1)), 4);
    /* the columns in view, the furthest round first */
    for (c = 0; c < ST_COLS; c++) {
        int8_t r = rel_of(c);
        uint8_t a = r < 0 ? -r : r;
        if (a >= GFX_NANG) continue;
        for (j = ncols; j > 0; j--) {
            int8_t q = rel_of(cols[j - 1]);
            if ((q < 0 ? -q : q) >= a) break;
            cols[j] = cols[j - 1];
        }
        cols[j] = c;
        ncols++;
    }
    /* the highest row with a block: the rows above it are empty */
    {
        const uint8_t *b = &g->board[ST_COLS * ST_TOTAL];
        for (top = ST_TOTAL; top; top--) {
            b -= ST_COLS;
            if (memcmp(b, empty_row, ST_COLS)) break;
        }
    }
    for (k = 0; k < ncols; k++) {
        int8_t rel, row;
        uint8_t v, a, page, mirror;
        int16_t x;
        const uint8_t *p;
        const uint16_t *table;
        c = cols[k];
        rel = rel_of(c);
        a = rel < 0 ? -rel : rel;
        mirror = rel < 0;
        x = mirror ? GFX_VIEW_MIRROR : 0;
        page = brick_page[a];                   /* SET_BOARD's blocks come first */
        table = (const uint16_t *)brick_addr[a];
        /* rows above the eye from the top down, then from the bottom up */
        p = &g->board[(uint16_t)(top - 1) * ST_COLS + c];
        for (row = top - 1; row >= EYE_ROW + 1; row--, p -= ST_COLS)
            if ((v = *p)) column_brick(page, table, (uint8_t)row, x, mirror, lit_pen(v));
        p = &g->board[c];
        for (row = 0; row < top && row <= EYE_ROW; row++, p += ST_COLS)
            if ((v = *p)) column_brick(page, table, (uint8_t)row, x, mirror, lit_pen(v));
    }
}

/* the drop guide: in each of the piece's columns, from its highest cell
   down to the ghost's lowest. The core's panels each have a pen of their
   own; the copper turns the pens of the piece's columns to the guide's
   colour on the lines between those rows and back: the guide is on the
   core and blocks in front cover it, as the original draws it first.
   port: its top and bottom edges are whole lines (the original's follow
   the perspective, a pixel or two off at the sides) */
#define MAX_GUIDE 10
static uint8_t guide_n, guide_y[MAX_GUIDE], guide_pen[MAX_GUIDE], guide_on[MAX_GUIDE];
static uint8_t guide_shown = 1;

/* an event, kept in order of rows (the copper's order) */
static void guide_event(uint8_t y, uint8_t pen, uint8_t on)
{
    uint8_t j;
    if (guide_n >= MAX_GUIDE) return;
    for (j = guide_n; j > 0 && guide_y[j - 1] > y; j--) {
        guide_y[j] = guide_y[j - 1];
        guide_pen[j] = guide_pen[j - 1];
        guide_on[j] = guide_on[j - 1];
    }
    guide_y[j] = y;
    guide_pen[j] = pen;
    guide_on[j] = on;
    guide_n++;
}

static void guide_events(int8_t ghost_row)
{
    int8_t hi[ST_COLS], lo[ST_COLS], cells[5][2];
    Piece gp = game.piece;
    uint8_t n, c, k, a;
    for (c = 0; c < ST_COLS; c++) { hi[c] = -1; lo[c] = 127; }
    n = st_cells_of(&game.piece, cells);
    for (k = 0; k < n; k++) if (cells[k][1] > hi[cells[k][0]]) hi[cells[k][0]] = cells[k][1];
    gp.row = ghost_row;
    n = st_cells_of(&gp, cells);
    for (k = 0; k < n; k++) if (cells[k][1] < lo[cells[k][0]]) lo[cells[k][0]] = cells[k][1];
    ZXN_WRITE_REG(0x50, GFX_PAL_PAGE);          /* the guide's rows table */
    for (c = 0; c < ST_COLS; c++) {
        int8_t rel = rel_of(c), h = hi[c], l = lo[c];
        const uint8_t *ys;
        uint8_t y0, y1, pen;
        if (h < 0 || l > h) continue;
        a = rel < 0 ? -rel : rel;
        if (a >= GFX_NANG) continue;
        if (h >= ST_TOTAL) h = ST_TOTAL - 1;
        if (l < 0) l = 0;
        ys = (const uint8_t *)(GFX_GUIDE_Y + a * (ST_TOTAL + 1));
        y0 = ys[h + 1];
        y1 = ys[l];
        if (y1 <= y0) continue;
        pen = GFX_PEN_SLOT + ((c - (vstep >> 3)) & (ST_COLS - 1));
        guide_event(y0, pen, 1);
        guide_event(y1, pen, 0);
    }
}

/* after the flip: the copper list for the frame now shown */
void tower_copper(void)
{
    uint8_t k;
    if (!guide_n && !guide_shown) return;
    copper_begin();
    copper_move(0x43, 0x10);                    /* palette writes: Layer 2's first */
    for (k = 0; k < guide_n; k++) {
        uint16_t rgb = guide_on[k] ? GUIDE_RGB : CORE_RGB;
        if (guide_y[k]) split_row(guide_y[k]);
        copper_move(0x40, guide_pen[k]);
        copper_move(0x44, (uint8_t)(rgb >> 1));
        copper_move(0x44, rgb & 1);
    }
    copper_end();
    guide_shown = guide_n;
}

void tower_guide_off(void)
{
    guide_n = 0;
    tower_copper();
}

/* the board as last drawn (base picture and blocks), kept in four pages so
   a frame where only the piece moved starts from a copy of it */
static uint8_t cache_step = 0xFF;
static uint16_t cache_ver;
static uint32_t cache_seed;

void tower_invalidate(void) { cache_step = 0xFF; }

void tower_draw(uint8_t with_piece, uint8_t guide)
{
    int8_t gr = 0;
    const StGame *g = &game;

    tower_bricks = 0;
    guide_n = 0;
    vstep = (uint8_t)(stack.view >> 5) & 127;
    if (cache_step != vstep || cache_ver != g->board_ver || cache_seed != g->seed) {
        draw_board();
        dma_copy_pages(GFX_CACHE_PAGE, l2w_bank * 2 + GFX_BASE_X0 / 32, 4);
        cache_step = vstep;
        cache_ver = g->board_ver;
        cache_seed = g->seed;
        tower_redraws++;
    } else {
        dma_copy_pages(l2w_bank * 2 + GFX_BASE_X0 / 32, GFX_CACHE_PAGE, 4);
    }

    if (with_piece) {                   /* the ghost's row, worked out again only when something moved */
        static Piece gp_last;
        static uint16_t ver_last = 0xFFFF;
        static int8_t gr_last;
        if (ver_last != g->board_ver || memcmp(&gp_last, &g->piece, sizeof(Piece))) {
            gr_last = st_ghost_row(g);
            gp_last = g->piece;
            ver_last = g->board_ver;
        }
        gr = gr_last;
        if (guide) guide_events(gr);
        {
            uint8_t v = g->piece.type + 1;
            if (stack.ghost && gr != g->piece.row) {
                Piece gp = g->piece;
                gp.row = gr;
                piece_bricks(SET_GHOST, &gp, GFX_PEN_GHOST + 2 * (v - 1));   /* an outline in the piece's colour */
            }
            piece_bricks(SET_PIECE, &g->piece, lit_pen(v));
        }
    }
    ZXN_WRITE_REG(0x50, 0xFF);          /* the ROM back in slot 0 */
}
