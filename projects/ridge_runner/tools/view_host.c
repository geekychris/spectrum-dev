/* The view on the host: an autopilot race on a track, frames drawn with
   the same scene (src/view.c) and the same fill rule (src/cpoly.h) as the
   Next, written as PPM pictures (line doubled to 320 x 256) for a look and
   for test.py to compare with the Next's Layer 2.
     view_host <track> <seed> <ticks...>   -> build/host/view_<tick>.ppm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "view.h"

static uint8_t fb[VIEW_W][VIEW_H];      /* column-major, as Layer 2's 320 mode */

static void fill(const int16_t *xy, uint8_t n, uint8_t pen)
{
    int16_t x, xmin = 32767, xmax = -32767;
    uint8_t i;
    for (i = 0; i < n; i++) {
        if (xy[2 * i] < xmin) xmin = xy[2 * i];
        if (xy[2 * i] > xmax) xmax = xy[2 * i];
    }
    if (xmin < 0) xmin = 0;
    if (xmax > VIEW_W) xmax = VIEW_W;
    for (x = xmin; x < xmax; x++) {
        int32_t lo = 32767, hi = -32767, y;
        for (i = 0; i < n; i++) {
            int32_t x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = xy[2 * ((i + 1) % n)], y1 = xy[2 * ((i + 1) % n) + 1], s;
            if (x0 == x1) continue;
            if (x < (x0 < x1 ? x0 : x1) || x >= (x0 < x1 ? x1 : x0)) continue;
            s = (int32_t)(((int64_t)(y1 - y0) << 16) / (x1 - x0));
            y = (int32_t)((((int64_t)y0 << 16) + (int64_t)s * (x - x0) + 0x8000) >> 16);
            if (y < lo) lo = y;
            if (y > hi) hi = y;
        }
        if (lo < 0) lo = 0;
        if (hi > VIEW_H) hi = VIEW_H;
        for (y = lo; y < hi; y++) fb[x][y] = pen;
    }
}

static void save(const char *path, uint8_t theme)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    fprintf(f, "P6\n%d %d\n255\n", VIEW_W, 2 * VIEW_H);
    for (y = 0; y < 2 * VIEW_H; y++)
        for (x = 0; x < VIEW_W; x++) {
            uint16_t c = view_themes[theme][fb[x][y / 2]];
            int b;
            for (b = 8; b >= 0; b -= 4) fputc(((c >> b) & 15) * 17, f);
        }
    fclose(f);
}

int main(int argc, char **argv)
{
    static RRGame g;
    int track = argc > 1 ? atoi(argv[1]) : 1, a, t = 0;
    uint32_t seed = argc > 2 ? (uint32_t)atol(argv[2]) : 1;
    int32_t cam_yaw;
    ViewIn in;
    rr_new_game(&g, seed, track);
    view_new_track(&g);
    cam_yaw = g.yaw;
    for (a = 3; a < argc; a++) {
        int want = atoi(argv[a]);
        char path[64];
        uint16_t n;
        for (; t < want && !g.finished && !g.over; t++) {
            rr_tick(&g, rr_bot(&g));
            cam_yaw = (cam_yaw + (int32_t)(int16_t)(g.yaw - cam_yaw) / 4) & 0xFFFF;
        }
        in.cam_yaw = cam_yaw;
        in.show_player = 1;
        n = view_build(&g, &in);
        memset(fb, 0, sizeof(fb));
        view_draw(fill);
        snprintf(path, sizeof(path), "build/host/view_%d.ppm", want);
        save(path, view_theme(g.track));
        printf("%s: %u polygons, z %ld, bikes %d%d%d%d\n", path, n, (long)(g.z >> 16), view_bikes[0].on,
               view_bikes[1].on, view_bikes[2].on, view_bikes[3].on);
    }
    return 0;
}
