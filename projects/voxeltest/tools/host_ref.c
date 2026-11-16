/* voxeltest's views drawn on the host by voxel.c's C reference: the sums
   the Next must log, and PPM pictures.
     cc -I../src -I../../../sdk/include host_ref.c ../src/scene.c
        ../../../sdk/src/next/voxel.c ../../../sdk/src/fix16.c ../../../sdk/src/fix16_tab.c */
#include <stdio.h>
#include <string.h>
#include "scene.h"

static uint8_t fb[256 * 96];

int main(int argc, char **argv)
{
    uint8_t k;
    vox_host_fb = fb;
    scene_make();
    for (k = 0; k < SCENE_VIEWS; k++) {
        uint16_t s = 0;
        uint32_t i;
        scene_view(k);
        memset(fb, 0, sizeof(fb));
        vox_draw();
        for (i = 0; i < sizeof(fb); i++) s = (uint16_t)(s * 31 + fb[i]);
        printf("view %u sum %u\n", k, s);
        if (argc > 1) {
            char path[256];
            FILE *f;
            snprintf(path, sizeof(path), "%s/view%u.pgm", argv[1], k);
            f = fopen(path, "wb");
            fprintf(f, "P5\n256 96\n255\n");
            fwrite(fb, 1, sizeof(fb), f);
            fclose(f);
        }
    }
    return 0;
}
