/*
 * Host program for tools/gen_gfx.py: draws every tile and sprite with the
 * original's own functions (tools/amiga_draw.c) and prints each as rows of
 * pen numbers (hex digits, '.' = nothing drawn), one "name w h" header each.
 */
#include <stdio.h>
#include <string.h>
#include "amiga_shim.h"
#define draw_character_preview unused_preview   /* title.c's, not needed */
#include "amiga_draw.c"

unsigned char fb[FB_H][FB_W];
Game game;

void SetAPen(struct RastPort *rp, long pen) { rp->pen = (int)pen; }
static void plot(int x, int y, int pen)
{
    if (x >= 0 && y >= 0 && x < FB_W && y < FB_H) fb[y][x] = (unsigned char)pen;
}
void RectFill(struct RastPort *rp, long x0, long y0, long x1, long y1)
{
    long x, y;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) plot((int)x, (int)y, rp->pen);
}
void Move(struct RastPort *rp, long x, long y) { rp->cx = (int)x; rp->cy = (int)y; }
void Draw(struct RastPort *rp, long x, long y)
{
    int x0 = rp->cx, y0 = rp->cy, x1 = (int)x, y1 = (int)y;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        plot(x0, y0, rp->pen);
        if (x0 == x1 && y0 == y1) break;
        { int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
    }
    rp->cx = x1; rp->cy = y1;
}

static void clear(void) { memset(fb, 255, sizeof(fb)); }
static void dump(const char *name, int x, int y, int w, int h)
{
    int r, c;
    printf("%s %d %d\n", name, w, h);
    for (r = 0; r < h; r++) {
        for (c = 0; c < w; c++) putchar(fb[y + r][x + c] == 255 ? '.' : "0123456789abcdef"[fb[y + r][x + c]]);
        putchar('\n');
    }
}

int main(void)
{
    struct RastPort rp = {0, 0, 0};
    char name[32];
    int t, ch, dir, leg, f;
    static const int etypes[4] = { ENT_BEETLE, ENT_FLY, ENT_SPIDER, ENT_ANT };
    static const int itypes[10] = { ENT_BOARD, ENT_DISKETTE, ENT_BALL, ENT_PILLOW_C, ENT_PLANT,
                                    ENT_BRIEFCASE, ENT_HEART, ENT_PILLOW_P, ENT_BEER, ENT_WINE };
    for (t = 0; t < 16; t++) {
        clear();
        gfx_draw_tile(&rp, t, 0, 0);
        sprintf(name, "tile%d", t);
        dump(name, 0, 0, 16, 16);
    }
    /* the player: walk_frame 1 and 3 move the legs by +2 / -2 */
    for (ch = 0; ch < 2; ch++)
        for (dir = 0; dir < 2; dir++)
            for (leg = 0; leg < 3; leg++) {
                Player p;
                memset(&p, 0, sizeof(p));
                p.x = 16;
                p.y = 8;
                p.character = (BYTE)ch;
                p.dir = dir ? DIR_LEFT : DIR_RIGHT;
                p.state = leg ? 1 : 0;
                p.walk_frame = (BYTE)(leg == 1 ? 1 : leg == 2 ? 3 : 0);
                clear();
                player_draw(&rp, &p, 0);
                sprintf(name, "player%d_%d_%d", ch, dir, leg);
                dump(name, 16, 8, 16, 28);
            }
    for (t = 0; t < 4; t++)
        for (f = 0; f < 2; f++) {
            memset(&game, 0, sizeof(game));
            game.enemies[0].type = (BYTE)etypes[t];
            game.enemies[0].alive = 1;
            game.enemies[0].frame = (BYTE)f;
            game.enemies[0].x = 16;
            game.enemies[0].y = 16;
            clear();
            enemies_draw(&rp, 0);
            sprintf(name, "enemy%d_%d", etypes[t], f);
            dump(name, 16, 16, 16, 16);
        }
    for (t = 0; t < 10; t++) {
        memset(&game, 0, sizeof(game));
        game.items[0].type = (BYTE)itypes[t];
        game.items[0].active = 1;
        game.items[0].x = 16;
        game.items[0].y = 16;
        clear();
        items_draw(&rp, 0);
        sprintf(name, "item%d", itypes[t]);
        dump(name, 16, 16, 16, 16);
    }
    for (f = 0; f < 2; f++) {
        clear();
        draw_heart(&rp, 0, 0, f);
        sprintf(name, "heart%d", f);
        dump(name, 0, 0, 8, 8);
    }
    return 0;
}
