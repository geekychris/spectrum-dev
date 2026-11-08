/*
 * host_view.c - the view (src/view.c, vfill.c, vcam.c) on the host: a
 * level, flown by the autopilot, drawn every `every` steps into PPM
 * pictures (the drawn rows doubled, as the copper shows them), for
 * looking at the picture without the emulator.
 *
 *   host_view LEVELDIR OUTPREFIX world level steps every
 *
 * Built from projects/lander_3d (build/levels made by the game's build):
 *   S=../../sdk/src; cc -O2 -Isrc -I../../sdk/include tools/host_view.c
 *     tools/host_ter.c src/view.c src/vfill.c src/vcam.c src/vproj_model.c
 *     src/ld_core.c src/ld_land.c src/ld_level.c src/ld_auto.c $S/fix16.c $S/fix16_tab.c
 *     $S/fix16_atan.c $S/fix16_hyp3.c -o host_view
 */
#include <stdio.h>
#include <stdlib.h>
#include "ld.h"
#include "view.h"
#include "vfill.h"
#include "pens.h"

void host_ter_load(const char *dir);
static Level L;

static void save(const char *path, int world)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    fprintf(f, "P6 %d %d 255\n", VW, VROWS * 2);
    for (y = 0; y < VROWS * 2; y++)
        for (x = 0; x < VW; x++) {
            uint16_t c = ld_palettes[world][vf_host_fb[(y / 2) * VW + x]];
            fputc((c >> 8 & 15) * 17, f); fputc((c >> 4 & 15) * 17, f); fputc((c & 15) * 17, f);
        }
    fclose(f);
}

int main(int argc, char **argv)
{
    int w, lv, steps, every, t, n = 0;
    char path[256];
    if (argc < 7) return 2;
    host_ter_load(argv[1]);
    w = atoi(argv[3]); lv = atoi(argv[4]); steps = atoi(argv[5]); every = atoi(argv[6]);
    ld_new_level(&L, w, lv);
    view_init();
    view_reset();
    for (t = 0; t <= steps && !L.outcome; t++) {
        LdInput in;
        if (t % 4 == 0) view_tick(&L);
        if (t % every == 0) {
            view_camera(&L);
            view_draw(&L, (uint8_t)(t / 4));
            snprintf(path, sizeof(path), "%s_%03d.ppm", argv[2], n++);
            save(path, w);
            printf("%s t=%d agl=%ld polys=%u\n", path, t, (long)(ld_agl(&L) >> 16), view_polys);
        }
        ld_autopilot(&L, &in);
        ld_step(&L, &in);
    }
    return 0;
}
