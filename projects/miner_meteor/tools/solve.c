/*
 * Host-side solver: a breadth-first search over the game's own game_tick()
 * (src/game.c compiled for the host), one layer per tick. Guardians move the
 * same way whatever the player does, so states in a layer differ only by
 * the player, the keys and the floors; duplicates are dropped by a hash of
 * those. When a layer would pass MAX_STATES, the states with the most keys
 * collected are expanded first and the rest dropped (a beam), which only
 * cavern 6 needs. Prints, per cavern, the fastest route as runs of input flags.
 *
 *   tools/solve.sh            (builds this and writes tools/routes.json + src/demo.h)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

#define MAX_LAYERS 3000
#define MAX_STATES 200000

typedef struct { uint32_t parent; uint8_t input; } Link;

static GameState *cur, *nxt;
static Link *links[MAX_LAYERS];
static uint32_t counts[MAX_LAYERS];
static uint64_t *table;
static uint16_t *stamp;                  /* layer that wrote table[i] (no clearing) */
static uint16_t cur_layer;
static const uint32_t TABLE = 1u << 21;

static uint64_t key_of(const GameState *s)
{
    uint64_t h = 1469598103934665603ull;
    const uint8_t *p;
    size_t i;
#define MIX(ptr, n) for (p = (const uint8_t *)(ptr), i = 0; i < (n); i++) { h ^= p[i]; h *= 1099511628211ull; }
    MIX(&s->player.b.x, 2) MIX(&s->player.b.y, 2) MIX(&s->player.b.vy, 2)
    MIX(&s->player.b.on_ground, 1) MIX(&s->player.jump_lock, 1)
    MIX(s->kind, sizeof(s->kind)) MIX(&s->num_falling, 1)
    for (i = 0; i < s->num_falling; i++) { h ^= s->falling[i].cell * 31u + s->falling[i].timer; h *= 1099511628211ull; }
    return h | 1;
}

static int seen(uint64_t k)
{
    uint32_t i = (uint32_t)(k >> 7) & (TABLE - 1);
    while (stamp[i] == cur_layer) {
        if (table[i] == k) return 1;
        i = (i + 1) & (TABLE - 1);
    }
    stamp[i] = cur_layer;
    table[i] = k;
    return 0;
}

/* order layer `layer` (cur, links[layer]) by keys collected, most first */
static uint32_t *order;
static GameState *tmp;
static Link *ltmp;
static const GameState *sort_base;
static int by_keys(const void *a, const void *b)
{
    const GameState *x = &sort_base[*(const uint32_t *)a], *y = &sort_base[*(const uint32_t *)b];
    if (x->keys_remaining != y->keys_remaining) return x->keys_remaining - y->keys_remaining;
    return (int)(*(const uint32_t *)a) - (int)(*(const uint32_t *)b);
}
static void sort_layer(uint32_t layer, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) order[i] = i;
    sort_base = cur;
    qsort(order, n, sizeof(uint32_t), by_keys);
    for (i = 0; i < n; i++) { tmp[i] = cur[order[i]]; ltmp[i] = links[layer][order[i]]; }
    memcpy(cur, tmp, sizeof(GameState) * n);
    memcpy(links[layer], ltmp, sizeof(Link) * n);
}

static const uint8_t inputs[6] = { 0, INPUT_LEFT, INPUT_RIGHT, INPUT_JUMP,
                                   INPUT_LEFT | INPUT_JUMP, INPUT_RIGHT | INPUT_JUMP };

static int solve(uint8_t level, FILE *json, FILE *demo, int last)
{
    uint32_t n = 1, layer, i, goal = 0;
    int found = -1;
    static uint8_t path[MAX_LAYERS];
    game_init();
    game_load_level(level);
    gs.mode = GM_PLAYING;
    gs.level_index = level;
    cur[0] = gs;
    for (layer = 0; layer < MAX_LAYERS - 1 && found < 0; layer++) {
        uint32_t m = 0;
        cur_layer = (uint16_t)(layer + 1);
        if (layer > 0) sort_layer(layer, n);
        links[layer + 1] = malloc(sizeof(Link) * MAX_STATES);
        for (i = 0; i < n && found < 0; i++) {
            int k;
            for (k = 0; k < 6; k++) {
                gs = cur[i];
                game_tick(inputs[k]);
                if (gs.events & EV_LEVEL) { found = (int)layer + 1; goal = i; path[layer] = inputs[k]; break; }
                if ((gs.events & EV_DIED) || gs.mode != GM_PLAYING) continue;
                if (seen(key_of(&gs))) continue;
                if (m == MAX_STATES) continue;  /* beam: parents with more keys came first */
                nxt[m] = gs;
                links[layer + 1][m].parent = i;
                links[layer + 1][m].input = inputs[k];
                m++;
            }
        }
        counts[layer + 1] = m;
        { GameState *t = cur; cur = nxt; nxt = t; }
        n = m;
        if (!n) break;
    }
    if (found < 0) { fprintf(stderr, "cavern %u: no route\n", level + 1); return 0; }
    {   /* walk back: goal is an index in layer found-1 */
        int L = found - 1;
        uint32_t idx = goal;
        while (L > 0) {
            path[L - 1] = links[L][idx].input;
            idx = links[L][idx].parent;
            L--;
        }
    }
    for (i = 1; i < (uint32_t)found + 1; i++) free(links[i]);
    fprintf(stderr, "cavern %u %-17s %d ticks (%.1f s)\n", level + 1, "", found, found / 50.0);
    /* json: [[input, count], ...] */
    fprintf(json, "  [");
    fprintf(demo, "    /* cavern %u: %d ticks */\n   ", level + 1, found);
    {
        int a = 0, col = 0;
        while (a < found) {
            int b = a;
            while (b < found && path[b] == path[a] && b - a < 255) b++;
            fprintf(json, "%s[%u, %d]", a ? ", " : "", path[a], b - a);
            fprintf(demo, " {%u, %3d},", path[a], b - a);
            if (++col % 8 == 0) fprintf(demo, "\n   ");
            a = b;
        }
    }
    fprintf(json, "]%s\n", last ? "" : ",");
    fprintf(demo, " {0xFF, 0},\n");
    return 1;
}

int main(int argc, char **argv)
{
    FILE *json, *demo;
    uint8_t l;
    int ok = 1;
    if (argc < 3) { fprintf(stderr, "usage: solve routes.json demo.h\n"); return 2; }
    cur = malloc(sizeof(GameState) * MAX_STATES);
    nxt = malloc(sizeof(GameState) * MAX_STATES);
    table = malloc(sizeof(uint64_t) * TABLE);
    stamp = calloc(TABLE, sizeof(uint16_t));
    order = malloc(sizeof(uint32_t) * MAX_STATES);
    tmp = malloc(sizeof(GameState) * MAX_STATES);
    ltmp = malloc(sizeof(Link) * MAX_STATES);
    json = fopen(argv[1], "w");
    demo = fopen(argv[2], "w");
    fprintf(json, "[\n");
    fprintf(demo, "/* made by tools/solve.sh: the fastest route through each cavern, found by a\n"
                  "   search over game_tick() on the host. {input flags, ticks}; 0xFF ends a cavern. */\n"
                  "static const uint8_t demo_route[][2] = {\n");
    for (l = 0; l < num_levels; l++) ok &= solve(l, json, demo, l == num_levels - 1);
    fprintf(json, "]\n");
    fprintf(demo, "};\n");
    fclose(json);
    fclose(demo);
    return ok ? 0 : 1;
}
