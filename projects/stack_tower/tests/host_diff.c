/*
 * host_diff.c - the Next port's rules (src/st_core.c) against the
 * original's (amiga_games stack_tower/st_core.c, the browser game's in C):
 * random games of moves, turns, holds, drops, rubble and ticks, played by
 * both; after every action the board, piece, queue, score, level, rings and
 * events must be the same. The original is compiled in through orig_core.c
 * with its names prefixed.
 *
 *   make -C tests      (ORIG=path/to/amiga_games/stack_tower)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/st.h"
#include "orig_st.h"

static unsigned long rng = 12345;
static unsigned rnd(unsigned n) { rng = rng * 1103515245UL + 12345; return (unsigned)((rng >> 16) % n); }

static int same(const StGame *a, const OStGame *b, const char *what, int step)
{
    int i;
    if (memcmp(a->board, b->board, sizeof(a->board))) goto bad;
    if (a->piece.type != b->piece.type || a->piece.rot != ((b->piece.rot % 4) + 4) % 4 || a->piece.col != b->piece.col ||
        a->piece.row != b->piece.row) goto bad;
    if (a->score != b->score || a->level != b->level || a->lines != b->lines || a->combo != b->combo) goto bad;
    if (a->over != b->over || a->won != b->won || a->hold != b->hold || a->pieces != b->pieces) goto bad;
    if (a->nqueue != b->nqueue) goto bad;
    for (i = 0; i < a->nqueue; i++) if (a->queue[i] != b->queue[i]) goto bad;
    if (a->nev != b->nev) goto bad;
    for (i = 0; i < a->nev; i++) if (a->ev[i].type != b->ev[i].type || a->ev[i].a != b->ev[i].a) goto bad;
    return 1;
bad:
    printf("FAIL after %s at step %d: score %ld/%ld rows %d/%d\n", what, step, (long)a->score, (long)b->score,
           a->piece.row, b->piece.row);
    return 0;
}

/* the title demo's choice (game.c demo_step): the lowest landing place,
   set on both games as the demo does */
static void place(StGame *a, OStGame *b)
{
    int best = 1 << 30, bc = a->piece.col, br = a->piece.rot, r, c;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 16; c++) {
            Piece p = a->piece;
            int8_t cells[5][2];
            int n, k, h = 0;
            p.rot = r;
            p.col = c;
            if (st_collides(a, &p)) continue;
            while (!st_collides(a, &p)) p.row--;
            p.row++;
            n = st_cells_of(&p, cells);
            for (k = 0; k < n; k++) h += cells[k][1] * 4;
            h += rnd(3);
            if (h < best) { best = h; bc = c; br = r; }
        }
    a->piece.rot = br; a->piece.col = bc; a->last_col = bc;
    b->piece.rot = br; b->piece.col = bc; b->last_col = bc;
}

int main(void)
{
    static StGame a;
    static OStGame b;
    int game, step, games = 300, steps = 3000, fails = 0;
    long pieces = 0, lines = 0;
    for (game = 0; game < games && !fails; game++) {
        int mode = game % 3, level = 1 + game % 7;
        unsigned long seed = 1 + game * 7919;
        st_new_game(&a, mode, seed, level, game % 16);
        ost_new_game(&b, mode, seed, level, game % 16);
        if (!same(&a, &b, "new", 0)) { fails++; break; }
        for (step = 0; step < steps && !a.over && !a.won; step++) {
            const char *what;
            switch (rnd(14)) {
            case 0: case 1: st_move(&a, 1); ost_move(&b, 1); what = "move"; break;
            case 2: case 3: st_move(&a, -1); ost_move(&b, -1); what = "move-"; break;
            case 4: st_rotate(&a, 1); ost_rotate(&b, 1); what = "rot"; break;
            case 5: st_rotate(&a, -1); ost_rotate(&b, -1); what = "rot-"; break;
            case 6: st_hard_drop(&a); ost_hard_drop(&b); what = "drop"; break;
            case 10: case 11: place(&a, &b); st_hard_drop(&a); ost_hard_drop(&b); what = "place"; break;
            case 7: st_hold(&a); ost_hold(&b); what = "hold"; break;
            case 8: { int on = rnd(2); st_soft_drop(&a, on); ost_soft_drop(&b, on); what = "soft"; break; }
            case 9: if (rnd(20) == 0) { int g = 1 + rnd(2); st_add_garbage(&a, g); ost_add_garbage(&b, g); } what = "garbage"; break;
            default: st_tick(&a, 40); ost_tick(&b, 40); what = "tick"; break;
            }
            if (!same(&a, &b, what, step)) { fails++; break; }
            if (rnd(4) == 0) { StEvent e; OStEvent f; while (st_pop_event(&a, &e)) ost_pop_event(&b, &f); }
        }
        pieces += a.pieces;
        lines += a.lines;
    }
    printf("%s: %d games, %ld pieces, %ld rings\n", fails ? "FAIL" : "PASS", game, pieces, lines);
    return fails != 0;
}
