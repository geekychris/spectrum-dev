/*
 * Host-side solver: plays the game's own jq_tick() (src/jq.c built for the
 * host) with a beam search. Every STEP ticks each state branches on six
 * inputs (nothing, left, right, jump, left + jump, right + jump); states are
 * merged by the player's position and speed, and the BEAM states furthest
 * right (then healthiest) go on. A level is solved when the level-complete
 * state is reached without losing a life; damage is allowed but ranked
 * lower. Prints the route of each level as runs of input flags.
 *
 *   tools/solve.sh [jump_force]   (builds this; writes tools/routes.json + src/demo.h)
 *   build/solve -q                (just: which levels clear, for a JUMP_FORCE scan)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jq.h"

#define STEP 4
#define BEAM 4000
#define MAX_STEPS 1500
#define NACT 6

static const uint8_t acts[NACT] = {
    INP_RIGHT, INP_RIGHT | INP_JUMP, 0, INP_JUMP, INP_LEFT, INP_LEFT | INP_JUMP
};

typedef struct { int32_t parent; uint8_t act; } Link;

typedef struct {
    Game g;
    int32_t link;
    int32_t rank;
} Node;

static Node *cur, *nxt;
static int ncur, nnxt;
static Link *links;
static int nlinks, cap_links;

static int32_t add_link(int32_t parent, uint8_t act)
{
    if (nlinks == cap_links) {
        cap_links = cap_links ? cap_links * 2 : 1 << 20;
        links = realloc(links, sizeof(Link) * cap_links);
    }
    links[nlinks].parent = parent;
    links[nlinks].act = act;
    return nlinks++;
}

/* merge table: one state per (x, y, vy, on_ground, invuln bucket) */
#define HBITS 20
static uint64_t *hkey;
static int32_t *hidx;
static uint32_t *hstamp, stampv;

static uint64_t key_of(const Game *g)
{
    const Player *p = &g->player[g->active_player];
    uint64_t k = (uint64_t)(uint16_t)p->x;
    k = k * 1000003u + (uint16_t)p->y;
    k = k * 1000003u + (uint16_t)p->vy;
    k = k * 1000003u + (uint8_t)p->on_ground;
    k = k * 1000003u + (p->invuln ? 1 + p->invuln / 16 : 0);
    k = k * 1000003u + (uint8_t)p->speed_boost / 50;
    return k | 1;
}

static int32_t rank_of(const Game *g)
{
    const Player *p = &g->player[g->active_player];
    return (int32_t)p->x * 64 + p->health * 64 * 64;   /* a heart is worth 64 pixels */
}

static int cmp_rank(const void *a, const void *b)
{
    const Node *x = a, *y = b;
    return (y->rank > x->rank) - (y->rank < x->rank);
}

/* from `start` (a level's first tick to come), the route to its end */
static int solve_level(const Game *start, int level, uint8_t **route_out, int *len_out, int verbose)
{
    int step, i, a, t;
    game = *start;
    jq_restore();
    nlinks = 0;
    ncur = 1;
    cur[0].g = game;
    cur[0].link = -1;
    for (step = 0; step < MAX_STEPS; step++) {
        nnxt = 0;
        stampv++;
        for (i = 0; i < ncur; i++) {
            for (a = 0; a < NACT; a++) {
                Node *n = &nxt[nnxt];
                const Player *p;
                uint64_t k;
                uint32_t h;
                n->g = cur[i].g;
                game = n->g;
                jq_restore();
                for (t = 0; t < STEP; t++) {
                    jq_tick(acts[a]);
                    if (game.state != STATE_PLAYING) break;
                }
                if (game.state == STATE_DYING) continue;
                p = &game.player[0];
                if (game.state == STATE_LEVELWIN) {
                    /* done: the route back from here */
                    int32_t l = add_link(cur[i].link, acts[a]);
                    int n_steps = 0, j;
                    int32_t q;
                    uint8_t *r;
                    for (q = l; q >= 0; q = links[q].parent) n_steps++;
                    r = malloc(n_steps * STEP);
                    j = n_steps;
                    for (q = l; q >= 0; q = links[q].parent) {
                        j--;
                        memset(r + j * STEP, links[q].act, STEP);
                    }
                    *route_out = r;
                    *len_out = (n_steps - 1) * STEP + t + 1;
                    if (verbose)
                        fprintf(stderr, "level %d: %d ticks, health %d, score %ld\n", level + 1,
                                *len_out, p->health, (long)p->score);
                    return 1;
                }
                n->g = game;
                k = key_of(&n->g);
                h = (uint32_t)(k >> 11) & ((1u << HBITS) - 1);
                n->rank = rank_of(&n->g);
                while (hstamp[h] == stampv && hkey[h] != k) h = (h + 1) & ((1u << HBITS) - 1);
                if (hstamp[h] == stampv) {
                    Node *o = &nxt[hidx[h]];
                    if (n->rank > o->rank) {
                        o->g = n->g;
                        o->rank = n->rank;
                        o->link = add_link(cur[i].link, acts[a]);
                    }
                    continue;
                }
                hstamp[h] = stampv;
                hkey[h] = k;
                hidx[h] = nnxt;
                n->link = add_link(cur[i].link, acts[a]);
                nnxt++;
            }
        }
        if (!nnxt) break;
        if (nnxt > BEAM) {
            qsort(nxt, nnxt, sizeof(Node), cmp_rank);
            nnxt = BEAM;
        }
        { Node *s = cur; cur = nxt; nxt = s; ncur = nnxt; }
        if (verbose > 1 && step % 50 == 0) {
            int best = 0;
            for (i = 0; i < ncur; i++) if (cur[i].g.player[0].x > best) best = cur[i].g.player[0].x;
            fprintf(stderr, "  step %d: %d states, best x %d\n", step, ncur, best);
        }
    }
    {
        int best = 0;
        for (i = 0; i < ncur; i++) if (cur[i].g.player[0].x > best) best = cur[i].g.player[0].x;
        if (verbose) fprintf(stderr, "level %d: not solved (furthest x %d of %d)\n", level + 1, best,
                             levels[level].width * TILE_SIZE);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int quiet = argc > 1 && !strcmp(argv[1], "-q");
    const char *json = !quiet && argc > 1 ? argv[1] : NULL;
    const char *hdr = !quiet && argc > 2 ? argv[2] : NULL;
    uint8_t *route[NUM_LEVELS];
    int len[NUM_LEVELS], lv, ok = 0;
    FILE *f;

    cur = malloc(sizeof(Node) * BEAM * NACT);
    nxt = malloc(sizeof(Node) * BEAM * NACT);
    hkey = calloc(1u << HBITS, sizeof(uint64_t));
    hidx = calloc(1u << HBITS, sizeof(int32_t));
    hstamp = calloc(1u << HBITS, sizeof(uint32_t));
    fprintf(stderr, "JUMP_FORCE %d\n", JUMP_FORCE);
    /* the levels in a row, as the demo plays them: each starts where the
       last ended (score, health), after the level-complete pause */
    jq_new_game(CHAR_RJ, 1);
    for (lv = 0; lv < NUM_LEVELS; lv++) {
        Game start = game;
        int t;
        route[lv] = NULL;
        len[lv] = 0;
        if (!solve_level(&start, lv, &route[lv], &len[lv], 1)) break;
        ok++;
        game = start;                      /* play the route again to its end */
        jq_restore();
        for (t = 0; t < len[lv]; t++) jq_tick(route[lv][t]);
        while (game.state == STATE_LEVELWIN) jq_tick(0);
        fprintf(stderr, "  then: level %d, health %d, score %ld\n", game.current_level + 1,
                game.player[0].health, (long)game.player[0].score);
    }
    if (quiet || ok < NUM_LEVELS) return ok == NUM_LEVELS ? 0 : 1;

    /* routes.json: per level, [[flags, ticks], ...] */
    if (json && (f = fopen(json, "w"))) {
        fprintf(f, "{\n  \"jump_force\": %d,\n  \"routes\": [\n", JUMP_FORCE);
        for (lv = 0; lv < NUM_LEVELS; lv++) {
            int i = 0, first = 1;
            fprintf(f, "    [");
            while (i < len[lv]) {
                int j = i;
                while (j < len[lv] && route[lv][j] == route[lv][i]) j++;
                fprintf(f, "%s[%d, %d]", first ? "" : ", ", route[lv][i], j - i);
                first = 0;
                i = j;
            }
            fprintf(f, "]%s\n", lv + 1 < NUM_LEVELS ? "," : "");
        }
        fprintf(f, "  ]\n}\n");
        fclose(f);
    }
    /* demo.h: the same runs, 0xFF ends a level */
    if (hdr && (f = fopen(hdr, "w"))) {
        fprintf(f, "/* made by tools/solve.sh (JUMP_FORCE %d): the routes through the three\n"
                   "   levels, {input flags, ticks}; 0xFF ends a level */\n"
                   "static const uint8_t demo_route[][2] = {\n", JUMP_FORCE);
        for (lv = 0; lv < NUM_LEVELS; lv++) {
            int i = 0, n = 0;
            fprintf(f, "    ");
            while (i < len[lv]) {
                int j = i, r;
                while (j < len[lv] && route[lv][j] == route[lv][i] && j - i < 255) j++;
                r = j - i;
                fprintf(f, "{%d, %d},%s", route[lv][i], r, (++n % 8) ? " " : "\n    ");
                i = j;
            }
            fprintf(f, "{0xFF, 0},\n");
        }
        fprintf(f, "};\n");
        fclose(f);
    }
    return 0;
}
