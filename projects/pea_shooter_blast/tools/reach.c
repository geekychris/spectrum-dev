/*
 * reach.c - can a level's exit gate be reached at all? A breadth-first
 * search over the original's rules (src/game.c, src/levels.c) from the level's
 * start: each step holds one of 7 inputs (right, right + jump, left, left +
 * jump, jump / climb, down, nothing) for 8 frames; enemies are removed and
 * any damage (spikes) ends that branch. States are told apart by the tank's
 * position (4-pixel cells) and vertical speed. ORIGINAL_BUGS.md.
 *
 *   sh tools/reach.sh                  the original as it is, then with the port's fixes
 *   build/host/reach --plan N          with the fixes: the shortest way through
 *                                      level N (1-3), a line per 8 frames: the
 *                                      input (R right, J right + jump, L left,
 *                                      K left + jump, U jump / climb, D down,
 *                                      . nothing) and the tank's x and y when
 *                                      it starts; test.py's bot follows it
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "port_fixes.h"

void sfx_shoot(void) {}
void sfx_hit(void) {}
void sfx_explode(void) {}
void sfx_powerup(void) {}
void sfx_jump(void) {}
void sfx_player_hit(void) {}

#define MAXN 1500000
typedef struct { GameState g; int parent; unsigned char act; } Node;
static Node *q;
static unsigned char seen[MAP_W * TILE_W / 4][MAP_H * TILE_H / 4][8];
static const int acts[7][4] = { {0,1,0,0}, {0,1,1,0}, {1,0,0,0}, {1,0,1,0}, {0,0,1,0}, {0,0,0,1}, {0,0,0,0} };

/* the path to node i: each step's input and where the tank was before it */
static void plan_out(int i)
{
    static const char letter[] = "RJLKUD.";
    if (q[i].parent < 0) return;
    plan_out(q[i].parent);
    printf("%c %ld %ld\n", letter[q[i].act], (long)FIX_INT(q[q[i].parent].g.tank.x),
           (long)FIX_INT(q[q[i].parent].g.tank.y));
}

static int search(int lvl, int fixed, int plan)
{
    int head = 0, tail = 1, maxx = 0;
    memset(seen, 0, sizeof(seen));
    game_init(&q[0].g);
    if (fixed) port_fixes(&q[0].g);
    game_load_level(&q[0].g, (WORD)lvl);
    q[0].parent = -1;
    while (head < tail && tail < MAXN - 8) {
        int a;
        for (a = 0; a < 7; a++) {
            GameState g = q[head].g;
            int f, x, y, d, hurt = 0;
            for (f = 0; f < 8; f++) {
                int i, h = g.tank.health;
                for (i = 0; i < MAX_ENEMIES; i++) g.enemies[i].active = 0;
                game_update(&g, acts[a][0], acts[a][1], acts[a][2], acts[a][3], 0);
                if (g.tank.health < h || g.state == STATE_DEAD) { hurt = 1; break; }
                g.tank.invuln_timer = 0;
                if (g.state == STATE_LEVELCLEAR) {
                    if (plan) {
                        plan_out(head);
                        printf("%c %ld %ld\n", "RJLKUD."[a], (long)FIX_INT(q[head].g.tank.x),
                               (long)FIX_INT(q[head].g.tank.y));
                    } else printf("level %d: the gate can be reached\n", lvl + 1);
                    return 1;
                }
            }
            if (hurt) continue;
            x = FIX_INT(g.tank.x) / 4;
            y = FIX_INT(g.tank.y) / 4;
            d = (int)(g.tank.dy >> 16) + 4;
            if (y < 0) y = 0;
            if (y > 63) y = 63;
            if (d < 0) d = 0;
            if (d > 7) d = 7;
            if (seen[x][y][d]) continue;
            seen[x][y][d] = 1;
            if (FIX_INT(g.tank.x) > maxx) maxx = FIX_INT(g.tank.x);
            q[tail].g = g;
            q[tail].parent = head;
            q[tail++].act = (unsigned char)a;
        }
        head++;
    }
    printf("level %d: the gate CAN'T be reached: the tank gets no further than x %d (column %d)\n", lvl + 1, maxx,
           maxx / TILE_W);
    return 0;
}

int main(int argc, char **argv)
{
    int fixed = argc > 1 && !strcmp(argv[1], "--fixed"), l, ok = 1;
    q = malloc(sizeof(Node) * MAXN);
    if (argc > 2 && !strcmp(argv[1], "--plan")) return !search(atoi(argv[2]) - 1, 1, 1);
    printf(fixed ? "with the port's fixes (src/port_fixes.c):\n" : "the original:\n");
    for (l = 0; l < 3; l++) ok &= search(l, fixed, 0);
    return ok ? 0 : 1;
}
