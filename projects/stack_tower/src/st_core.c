/*
 * st_core.c - Stack Tower's rules (see st.h), line for line from the
 * original's st_core.c (the browser's stack-tower-core.js).
 */
#include <string.h>
#include "st.h"

/* Cells as [dx, dy] about the pivot, y up: two trominoes and six
 * pentominoes, none of the classic four-square pieces. */
const int8_t st_cells[ST_NPIECES][5][2] = {
    { { -1, 0 }, { 0, 0 }, { 1, 0 } },                        /* rod */
    { { 0, 0 }, { 1, 0 }, { 0, 1 } },                         /* elbow */
    { { -1, 1 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { 1, 1 } },   /* cup */
    { { 0, -1 }, { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },    /* flag */
    { { 0, -1 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 } },   /* star */
    { { -1, 1 }, { 0, 1 }, { 1, 1 }, { 0, 0 }, { 0, -1 } },   /* anchor */
    { { -1, 1 }, { -1, 0 }, { 0, 0 }, { 0, -1 }, { 1, -1 } }, /* stairs */
    { { -1, 1 }, { 0, 1 }, { 0, 0 }, { 0, -1 }, { 1, -1 } },  /* bolt */
};
const uint8_t st_ncells[ST_NPIECES] = { 3, 3, 5, 5, 5, 5, 5, 5 };
const char *const st_piece_names[ST_NPIECES] = { "ROD", "ELBOW", "CUP", "FLAG", "STAR", "ANCHOR", "STAIRS", "BOLT" };
const char *const st_mode_names[MODE_COUNT] = { "marathon", "rising", "dig" };
const char *const st_event_names[STE_COUNT] = { "none", "move", "rotate", "kick", "drop", "hold",
    "lock", "clear", "level", "garbage", "win", "over" };

/* tried in order after a rotation that collides: none, sideways, up, further */
static const int8_t kicks[10][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 2, 0 }, { -2, 0 },
                                     { 1, 1 }, { -1, 1 }, { 0, 2 }, { 0, -1 } };
static const int16_t line_score[7] = { 0, 100, 300, 600, 1000, 1500, 2100 };
/* gravity: 0.9 s a row at level 1, x0.82 a level, never under 1/30 s */
static const int16_t interval_tab[30] = { 900, 738, 605, 496, 407, 334, 274, 224, 184, 151, 124, 101,
    83, 68, 56, 46, 38, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33 };

#define WRAP(c) ((c) & (ST_COLS - 1))   /* port: st_mod(c, 16) as a mask (the same for negatives) */

int16_t st_interval_ms(int16_t level) { return interval_tab[level < 1 ? 0 : level > 30 ? 29 : level - 1]; }

int16_t st_rise_ms(int16_t level)
{
    int16_t v = 14000 - level * 1200;
    return v < 4000 ? 4000 : v;
}

uint32_t st_hash(const char *s)
{
    uint32_t h = 2166136261UL;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619UL; }
    return h;
}

/* mulberry32, as the browser's rng() */
uint32_t st_rand32(uint32_t *a)
{
    uint32_t t;
    *a += 0x6D2B79F5UL;
    t = *a;
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

/* floor(rand() * n), as the browser's: the top 32 bits of rand32 * n
   port: in two halves (no 64-bit multiply), exact for n < 65536 */
uint8_t st_rand_below(uint32_t *a, uint8_t n)
{
    uint32_t r = st_rand32(a);
    uint32_t lo = (uint32_t)(uint16_t)r * n, hi = (r >> 16) * n;
    return (uint8_t)((hi + (lo >> 16)) >> 16);
}

static void push_ev(StGame *g, uint8_t type, int16_t a, int16_t b)
{
    if (g->nev < ST_MAX_EVENTS) {
        StEvent *e = &g->ev[g->nev++];
        e->type = type;
        e->a = a;
        e->b = b;
    }
}

uint8_t st_pop_event(StGame *g, StEvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(&g->ev[0], &g->ev[1], (g->nev - 1) * sizeof(StEvent));
    g->nev--;
    return 1;
}

/* a piece's cells after rot quarter turns clockwise: (x, y) -> (y, -x) */
uint8_t st_cells_of(const Piece *p, int8_t out[5][2])
{
    uint8_t n = st_ncells[p->type], i, k, r = p->rot & 3;
    const int8_t *c = &st_cells[p->type][0][0];
    for (i = 0; i < n; i++, c += 2) {
        int8_t x = c[0], y = c[1], t;
        for (k = 0; k < r; k++) { t = x; x = y; y = -t; }
        out[i][0] = WRAP(p->col + x);
        out[i][1] = p->row + y;
    }
    return n;
}

uint8_t st_collides(const StGame *g, const Piece *p)
{
    int8_t c[5][2];
    uint8_t n = st_cells_of(p, c), i;
    for (i = 0; i < n; i++) {
        int8_t r = c[i][1];
        if (r < 0 || r >= ST_TOTAL || g->board[((uint8_t)r << 4) | c[i][0]]) return 1;
    }
    return 0;
}

static void bag(StGame *g)
{
    int8_t b[ST_NPIECES], t;
    uint8_t i, j;
    for (i = 0; i < ST_NPIECES; i++) b[i] = i;
    for (i = ST_NPIECES - 1; i > 0; i--) {
        j = st_rand_below(&g->rng, i + 1);
        t = b[i];
        b[i] = b[j];
        b[j] = t;
    }
    for (i = 0; i < ST_NPIECES && g->nqueue < ST_QUEUE; i++) g->queue[g->nqueue++] = b[i];
}

static void refill(StGame *g) { while (g->nqueue < 8) bag(g); }

uint8_t st_preview(StGame *g, uint8_t n, int8_t *out)
{
    uint8_t i;
    refill(g);
    for (i = 0; i < n && i < g->nqueue; i++) out[i] = g->queue[i];
    return i;
}

static void garbage_row(StGame *g, uint8_t gaps, uint8_t *row)
{
    uint8_t freec[ST_COLS], nf = 0, i, c, seen;
    memset(row, ST_GARBAGE, ST_COLS);
    while (nf < gaps) {
        c = st_rand_below(&g->rng, ST_COLS);
        seen = 0;
        for (i = 0; i < nf; i++) if (freec[i] == c) seen = 1;
        if (!seen) freec[nf++] = c;
    }
    for (i = 0; i < nf; i++) row[freec[i]] = 0;
}

static void spawn(StGame *g, int8_t type, int8_t col)
{
    uint8_t fresh = type < 0;
    refill(g);
    if (fresh) {
        type = g->queue[0];
        memmove(&g->queue[0], &g->queue[1], g->nqueue - 1);
        g->nqueue--;
    }
    g->piece.type = type;
    g->piece.rot = 0;
    g->piece.col = WRAP(col);
    g->piece.row = ST_ROWS + 1;
    g->fall_ms = g->lock_ms = g->resets = 0;
    if (fresh) g->can_hold = 1;
    if (st_collides(g, &g->piece)) {
        g->piece.row++;                       /* try one higher before giving up */
        if (st_collides(g, &g->piece)) { g->over = 1; push_ev(g, STE_OVER, 0, 0); }
    }
}

/* the decimal digits of v */
static char *utoa32(char *p, uint32_t v)
{
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = '0' + (char)(v % 10); v /= 10; } while (v);
    while (n) *p++ = tmp[--n];
    return p;
}

void st_new_game(StGame *g, uint8_t mode, uint32_t seed, int16_t level, int8_t col)
{
    char key[40], *p;
    uint8_t r;
    memset(g, 0, sizeof(*g));
    g->mode = mode;
    g->seed = seed;
    p = utoa32(key, seed);                  /* port: sprintf(key, "%lu:%s", seed, mode name) */
    *p++ = ':';
    strcpy(p, st_mode_names[mode]);
    g->rng = st_hash(key);
    if (!g->rng) g->rng = 0x9e3779b9UL;
    g->hold = -1;
    g->can_hold = 1;
    g->level = g->start_level = level < 1 ? 1 : level;
    g->combo = -1;
    g->last_col = col;
    if (mode == MODE_DIG) {
        for (r = 0; r < ST_DIG_ROWS; r++) garbage_row(g, r % 3 == 0 ? 2 : 1, &g->board[r * ST_COLS]);
        g->garbage_left = ST_DIG_ROWS;
    }
    refill(g);
    spawn(g, -1, col);
}

static uint8_t touching(const StGame *g)
{
    Piece p = g->piece;
    p.row--;
    return st_collides(g, &p);
}

static void moved(StGame *g)
{
    /* a successful move or turn on the stack restarts the lock timer, a limited number of times */
    if (touching(g) && g->resets < ST_LOCK_RESETS) { g->lock_ms = 0; g->resets++; }
}

uint8_t st_move(StGame *g, int8_t dir)
{
    Piece p;
    if (g->over || g->won) return 0;
    p = g->piece;
    p.col = WRAP(p.col + dir);
    if (st_collides(g, &p)) return 0;
    g->piece = p;
    g->last_col = p.col;
    moved(g);
    push_ev(g, STE_MOVE, dir, 0);
    return 1;
}

uint8_t st_rotate(StGame *g, int8_t dir)
{
    uint8_t k;
    Piece p;
    if (g->over || g->won) return 0;
    for (k = 0; k < 10; k++) {
        p = g->piece;
        p.rot = (p.rot + dir) & 3;
        p.col = WRAP(p.col + (dir < 0 ? -kicks[k][0] : kicks[k][0]));   /* port: kicks[k][0] * dir */
        p.row += kicks[k][1];
        if (!st_collides(g, &p)) {
            g->piece = p;
            g->last_col = p.col;
            moved(g);
            push_ev(g, k ? STE_KICK : STE_ROTATE, dir, 0);
            return 1;
        }
    }
    return 0;
}

int8_t st_ghost_row(const StGame *g)
{
    Piece p = g->piece;
    while (1) {
        p.row--;
        if (st_collides(g, &p)) return p.row + 1;
    }
}

static uint8_t row_full(const uint8_t *row)
{
    uint8_t c;
    for (c = 0; c < ST_COLS; c++) if (!row[c]) return 0;
    return 1;
}

uint8_t st_count_garbage_rows(const StGame *g)
{
    uint8_t r, c, n = 0;
    const uint8_t *row = g->board;
    for (r = 0; r < ST_TOTAL; r++, row += ST_COLS)
        for (c = 0; c < ST_COLS; c++)
            if (row[c] == ST_GARBAGE) { n++; break; }
    return n;
}

/* port: the full rows found and removed in one pass (the original lists
   them, then copies the others); returns how many */
static uint8_t clear_full_rows(StGame *g)
{
    uint8_t r, k = 0;
    for (r = 0; r < ST_TOTAL; r++) {
        uint8_t *row = &g->board[r * ST_COLS];
        if (row_full(row)) continue;
        if (k != r) memcpy(&g->board[k * ST_COLS], row, ST_COLS);
        k++;
    }
    if (k < ST_TOTAL) memset(&g->board[k * ST_COLS], 0, (ST_TOTAL - k) * ST_COLS);
    return ST_TOTAL - k;
}

static void lock_piece(StGame *g)
{
    int8_t cells[5][2];
    uint8_t n = st_cells_of(&g->piece, cells), i, all_hidden = 1, nr;
    for (i = 0; i < n; i++) {
        g->board[((uint8_t)cells[i][1] << 4) | cells[i][0]] = (uint8_t)(g->piece.type + 1);
        if (cells[i][1] < ST_ROWS) all_hidden = 0;
    }
    g->board_ver++;
    g->pieces++;
    push_ev(g, STE_LOCK, g->piece.type, 0);
    if (all_hidden) { g->over = 1; push_ev(g, STE_OVER, 1, 0); return; }
    nr = clear_full_rows(g);
    if (nr) {
        int32_t pts;
        g->combo++;
        g->lines += nr;
        pts = (int32_t)line_score[nr < 7 ? nr : 6] * g->level + (g->combo > 0 ? 50L * g->combo * g->level : 0);
        g->score += pts;
        push_ev(g, STE_CLEAR, nr, (int16_t)pts);
        if (g->mode == MODE_DIG) {
            g->garbage_left = st_count_garbage_rows(g);
            if (g->garbage_left == 0) { g->won = 1; push_ev(g, STE_WIN, 0, 0); return; }
        }
        {
            int16_t lv = g->start_level + g->lines / 10;
            if (g->mode != MODE_DIG && lv > g->level) { g->level = lv; push_ev(g, STE_LEVEL, lv, 0); }
        }
    } else {
        g->combo = -1;
    }
    spawn(g, -1, g->last_col);
}

int8_t st_hard_drop(StGame *g)
{
    int8_t to, dist;
    if (g->over || g->won) return 0;
    to = st_ghost_row(g);
    dist = g->piece.row - to;
    g->piece.row = to;
    g->score += 2 * dist;
    push_ev(g, STE_DROP, dist, 0);
    lock_piece(g);
    return dist;
}

void st_soft_drop(StGame *g, uint8_t on) { g->soft = on != 0; }

uint8_t st_hold(StGame *g)
{
    int8_t cur;
    if (g->over || g->won || !g->can_hold) return 0;
    cur = g->piece.type;
    if (g->hold < 0) { g->hold = cur; spawn(g, -1, g->last_col); }
    else { int8_t t = g->hold; g->hold = cur; spawn(g, t, g->last_col); }
    g->can_hold = 0;
    push_ev(g, STE_HOLD, cur, 0);
    return 1;
}

void st_add_garbage(StGame *g, uint8_t gaps)
{
    uint8_t row[ST_COLS], c, spilled = 0;
    garbage_row(g, gaps, row);
    for (c = 0; c < ST_COLS; c++) if (g->board[(ST_TOTAL - 1) * ST_COLS + c]) spilled = 1;
    memmove(&g->board[ST_COLS], &g->board[0], ST_COLS * (ST_TOTAL - 1));
    memcpy(&g->board[0], row, ST_COLS);
    g->board_ver++;
    if (spilled) { g->over = 1; push_ev(g, STE_OVER, 2, 0); return; }
    if (st_collides(g, &g->piece)) {
        g->piece.row++;
        if (st_collides(g, &g->piece)) { g->over = 1; push_ev(g, STE_OVER, 2, 0); return; }
    }
    push_ev(g, STE_GARBAGE, 0, 0);
}

void st_tick(StGame *g, int16_t dt)
{
    int16_t step, iv;
    if (g->over || g->won) return;
    g->time_ms += dt;
    if (g->mode == MODE_RISING) {
        int16_t every = st_rise_ms(g->level);
        g->rise_ms += dt;
        if (g->rise_ms >= every) {
            g->rise_ms -= every;
            st_add_garbage(g, g->level >= 6 ? 1 : 2);
            if (g->over) return;
        }
    }
    iv = st_interval_ms(g->level);
    step = iv;
    if (g->soft) { int16_t s = iv / 20 > 17 ? iv / 20 : 17; step = s < iv ? s : iv; }
    if (touching(g)) {
        g->fall_ms = 0;
        g->lock_ms += dt;
        if (g->lock_ms >= ST_LOCK_MS || (g->resets >= ST_LOCK_RESETS && g->lock_ms >= ST_LOCK_MS / 3))
            lock_piece(g);
        return;
    }
    g->fall_ms += dt;
    while (g->fall_ms >= step && !touching(g)) {
        g->fall_ms -= step;
        g->piece.row--;
        if (g->soft) g->score += 1;
    }
    if (touching(g)) g->fall_ms = 0;
}
