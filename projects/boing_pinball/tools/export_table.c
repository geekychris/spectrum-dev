/*
 * export_table.c - builds Boing Pinball's tables with the original code
 * (amiga_mcp/examples/boing_pinball/pb_tables.c + pb_core.c, compiled on the
 * host) and writes them as JSON for tools/gen_table.py, which makes the
 * Spectrum Next's table data and pictures from it.
 *
 *   cc -O1 -I<amiga3d> -I<boing_pinball> export_table.c pb_core.c pb_tables.c a3d_fix.c a3d_math.c
 */
#include <stdio.h>
#include <stdint.h>
#include "a3d.h"
#include "a3d_fix.h"
#include "pb.h"

static Table T;
static double f(fix v) { return v / 65536.0; }

int main(int argc, char **argv)
{
    int which = argc > 1 ? argv[1][0] - '0' : 0, i, j;
    pb_build_table(&T, which);
    printf("{\"name\":\"%s\",\"blurb\":\"%s\",\"theme\":%d,\n", T.name, T.blurb ? T.blurb : "", T.theme);
    printf("\"mission\":[");
    for (i = 0; i < 5; i++) printf("%s\"%s\"", i ? "," : "", T.mission[i]);
    printf("],\n\"seg\":[\n");
    for (i = 0; i < T.nseg; i++) {
        Seg *s = &T.seg[i];
        printf("%s[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%d,%d]\n", i ? "," : "",
               f(s->ax), f(s->ay), f(s->bx), f(s->by), f(s->r), f(s->e), s->kind, s->id, s->ow_x, s->ow_y, s->bank, s->h, s->up);
    }
    printf("],\n\"circ\":[\n");
    for (i = 0; i < T.ncirc; i++) {
        Circ *c = &T.circ[i];
        printf("%s[%.4f,%.4f,%.4f,%.4f,%d,%d,%d]\n", i ? "," : "", f(c->cx), f(c->cy), f(c->r), f(c->e), c->bumper, c->id, c->h);
    }
    printf("],\n\"sens\":[\n");
    for (i = 0; i < T.nsens; i++) {
        Sensor *s = &T.sens[i];
        printf("%s[%.4f,%.4f,%.4f,%.4f,%d,%d]\n", i ? "," : "", f(s->ax), f(s->ay), f(s->bx), f(s->by), s->kind, s->idx);
    }
    printf("],\n\"flip\":[\n");
    for (i = 0; i < T.nflip; i++) {
        FlipDef *d = &T.flip[i];
        printf("%s[%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.6f,%.6f]\n", i ? "," : "", d->side, f(d->px), f(d->py), f(d->len), f(d->r1), f(d->r2), f(d->rest), f(d->up));
    }
    printf("],\n\"ramp\":[\n");
    for (i = 0; i < T.nramp; i++) {
        Ramp *r = &T.ramp[i];
        printf("%s{\"pts\":[", i ? "," : "");
        for (j = 0; j < r->n; j++) printf("%s[%d,%d,%d]", j ? "," : "", r->pts[j][0], r->pts[j][1], r->pts[j][2]);
        printf("],\"prof\":[");
        for (j = 0; j < r->n; j++) printf("%s%.4f", j ? "," : "", f(r->prof[j]));
        printf("],\"exit\":[%.4f,%.4f,%.4f]}\n", f(r->exit_x), f(r->exit_y), f(r->exit_vy));
    }
    printf("],\n\"spin\":[");
    for (i = 0; i < T.nspin; i++) printf("%s[%.4f,%.4f]", i ? "," : "", f(T.spin[i].x), f(T.spin[i].y));
    printf("],\n\"ndrop\":%d,\"nbank\":%d,\"drop_seg\":[", T.ndrop, T.nbank);
    for (i = 0; i < T.ndrop; i++) printf("%s%d", i ? "," : "", T.drop_seg[i]);
    printf("],\n\"saucer\":[%.4f,%.4f,%.4f],\"plunger\":[%.4f,%.4f],\n", f(T.saucer_x), f(T.saucer_y), f(T.saucer_r), f(T.plunger_x), f(T.plunger_rest));
    printf("\"lamp\":[");
    for (i = 0; i < L_COUNT; i++) printf("%s[%d,%d,%d,%d]", i ? "," : "", T.lamp[i].x, T.lamp[i].y, T.lamp[i].r, T.lamp[i].col);
    printf("],\n\"deco\":[\n");
    for (i = 0; i < T.ndeco; i++) {
        Deco *d = &T.deco[i];
        printf("%s[%d,%d,%d,%d,%d,%d,\"%s\",[%d,%d,%d,%d],[%d,%d,%d,%d]]\n", i ? "," : "", d->kind, d->col, d->x, d->y, d->a, d->b,
               d->text ? d->text : "", d->px[0], d->px[1], d->px[2], d->px[3], d->py[0], d->py[1], d->py[2], d->py[3]);
    }
    printf("],\n\"grid\":[\n");
    for (i = 0; i < PB_GY; i++) {
        printf("%s[", i ? "," : "");
        for (j = 0; j < PB_GX; j++) {
            int k;
            printf("%s[", j ? "," : "");
            for (k = 0; k < T.ngrid[i][j]; k++) printf("%s%d", k ? "," : "", T.grid[i][j][k]);
            printf("]");
        }
        printf("]\n");
    }
    printf("]}\n");
    return 0;
}
