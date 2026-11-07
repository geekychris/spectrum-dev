/* softlock.c - the original's barrel-on-jewel softlock, shown with the
   port's rules (src/wk_core.c = game.cpp): ROUTE (U D L R = the cursor
   keys) pushes the four barrels into a 2 x 2 block over the jewel at 6,6;
   then every state reachable from there is searched (breadth first, all
   four steps from each): none takes the last jewel.
     cc -O2 -I../src softlock.c ../src/wk_core.c -o softlock && ./softlock */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wk.h"

#define ROUTE "LUUULUURDDDD" "LDRRRR" "LUULDLDRR" "UUUURURDDD"

static uint32_t key(GameState *g)
{
    /* the player, the barrels (as a sorted set), the jewels left */
    uint8_t b[4], n = 0, i, j;
    uint32_t k;
    for (i = 0; i < g->num_objects; i++)
        if (g->objects[i].kind == O_BARREL) b[n++] = (uint8_t)(g->objects[i].wy * 8 + g->objects[i].wx);
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && b[j - 1] > b[j]; j--) { uint8_t t = b[j]; b[j] = b[j - 1]; b[j - 1] = t; }
    k = (uint32_t)(g->player.wy * 8 + g->player.wx);
    for (i = 0; i < n; i++) k = k * 64 + b[i];
    return k * 4 + g->jewels_left;
}

static uint8_t input_of(char c)
{
    return c == 'U' ? INPUT_UP : c == 'D' ? INPUT_DOWN : c == 'L' ? INPUT_LEFT : INPUT_RIGHT;
}

#define MAXQ 4096                              /* the player's 45 cells: plenty */
static GameState *queue;
static uint32_t *seen;
static size_t nseen;

static int was_seen(uint32_t k)
{
    size_t i;
    for (i = 0; i < nseen; i++) if (seen[i] == k) return 1;
    seen[nseen++] = k;
    return 0;
}

int main(void)
{
    GameState g;
    const char *r;
    size_t head = 0, tail = 0, won = 0;
    uint8_t i;
    wk_init(&g);
    wk_load_room(&g);
    g.mode = GM_PLAYING;
    for (r = ROUTE; *r; r++) wk_tick(&g, input_of(*r));
    printf("after the route (%u steps): player %d,%d, jewels left %u, mode %u, barrels",
           (unsigned)strlen(ROUTE), g.player.wx, g.player.wy, g.jewels_left, g.mode);
    for (i = 0; i < g.num_objects; i++)
        if (g.objects[i].kind == O_BARREL) printf(" %u,%u", g.objects[i].wx, g.objects[i].wy);
    for (i = 0; i < g.num_objects; i++)
        if (g.objects[i].kind == O_JEWEL) printf("; jewel at %u,%u", g.objects[i].wx, g.objects[i].wy);
    printf("\n");
    queue = malloc(sizeof(GameState) * MAXQ);
    seen = malloc(sizeof(uint32_t) * MAXQ);
    queue[tail++] = g;
    was_seen(key(&g));
    while (head < tail) {
        GameState s = queue[head++];
        const char *d;
        for (d = "UDLR"; *d; d++) {
            GameState t = s;
            t.time_left = WK_TIME;                     /* the clock aside: untimed, as the original */
            wk_tick(&t, input_of(*d));
            if (t.mode == GM_WIN) { won++; continue; }
            if (!was_seen(key(&t)) && tail < MAXQ) queue[tail++] = t;
        }
    }
    printf("states reachable from there: %u; any with the last jewel taken: %s\n", (unsigned)tail, won ? "YES" : "no");
    return won != 0;
}
