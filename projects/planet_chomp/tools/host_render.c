/*
 * host_render.c - the port's game and renderer on the host: plays the
 * attract demo and writes frames as PPM (256 x 192, the Next's palette), for
 * looking at the renderer without the emulator.
 *   host_render OUTDIR TICK...      frames at those ticks of the demo
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "pc.h"
#include "game.h"
#include "render.h"

extern const uint16_t rd_pal[C_COUNT];
static uint8_t fb[RD_H][RD_W];

void sfx_play(uint8_t id) { (void)id; }
void sfx_crumb(void) { }
void sfx_ambience(uint8_t w) { (void)w; }
void pc_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

void dr_begin(void) { }
void dr_end(void) { }
static void span(int y, int x0, int x1, uint8_t c)
{
    if (y < 0 || y >= RD_H) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 > RD_W - 1) x1 = RD_W - 1;
    for (; x0 <= x1; x0++) fb[y][x0] = c;
}
void dr_rect(int16_t y0, int16_t y1, int16_t x0, int16_t x1, uint8_t c)
{
    int y;
    for (y = y0; y < y1; y++) span(y, x0, x1, c);
}
/* convex polygon: rows y with ytop <= y < ybot, floor(x left) .. floor(x right) */
long st_polys, st_rows, st_px;
void dr_poly(const int16_t *xy, uint8_t n, uint8_t c)
{
    int y, i, ymin = 32767, ymax = -32767;
    st_polys++;
    for (i = 0; i < n; i++) { if (xy[i * 2 + 1] < ymin) ymin = xy[i * 2 + 1]; if (xy[i * 2 + 1] > ymax) ymax = xy[i * 2 + 1]; }
    if (ymin < 0) ymin = 0;
    if (ymax > RD_H) ymax = RD_H;
    for (y = ymin; y < ymax; y++) {
        double lo = 1e9, hi = -1e9, yc = y + 0.5;
        for (i = 0; i < n; i++) {
            double x0 = xy[i * 2], y0 = xy[i * 2 + 1], x1 = xy[(i + 1) % n * 2], y1 = xy[(i + 1) % n * 2 + 1], x;
            if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) {
                x = x0 + (yc - y0) * (x1 - x0) / (y1 - y0);
                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        }
        if (lo <= hi) { span(y, (int)lo, (int)hi, c); st_rows++; st_px += (int)hi - (int)lo + 1; }
    }
}

int main(int argc, char **argv)
{
    long t = 0;
    int a;
    game_init();
    for (a = 2; a < argc; a++) {
        long want = atol(argv[a]);
        char path[256];
        FILE *f;
        int x, y;
        while (t < want) {
            game_step(0, 0);
            t++;
        }
        memset(fb, C_SPACE, sizeof(fb));
        rd_frame();
        snprintf(path, sizeof(path), "%s/frame_%05ld.ppm", argv[1], t);
        f = fopen(path, "wb");
        fprintf(f, "P6\n%d %d\n255\n", RD_W, RD_H);
        for (y = 0; y < RD_H; y++)
            for (x = 0; x < RD_W; x++) {
                uint16_t c = rd_pal[fb[y][x]];
                fputc(((c >> 6) & 7) * 255 / 7, f);
                fputc(((c >> 3) & 7) * 255 / 7, f);
                fputc((c & 7) * 255 / 7, f);
            }
        fclose(f);
        printf("polys %ld rows %ld px %ld\n", st_polys, st_rows, st_px); st_polys = st_rows = st_px = 0;
        printf("tick %ld state %d walls %d crumbs %d h %d f %d,%d,%d up %d,%d,%d dot %d\n", t, g.state, rd_stats_walls, rd_stats_crumbs, g.cam.height, g.cam.focus.x, g.cam.focus.y, g.cam.focus.z, g.cam.up.x, g.cam.up.y, g.cam.up.z, v3_dot(&g.cam.focus, &g.cam.up));
    }
    return 0;
}
