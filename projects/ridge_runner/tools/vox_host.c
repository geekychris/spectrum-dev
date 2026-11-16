/* The voxel view on the host: an autopilot race, frames drawn by the SDK's
   voxel.c (the reference the Next's asm matches) from src/ground.c's maps,
   written as PPM (line doubled to 256 x 192).
     vox_host <track> <seed> <ticks...>   -> build/host/vox_<tick>.ppm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voxel.h"
#include "ground.h"
#include "view.h"

static uint8_t fb[256 * 96];

static void save(const char *path, uint8_t theme)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    fprintf(f, "P6\n256 192\n255\n");
    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x++) {
            uint8_t p = fb[(y / 2) * 256 + x];
            uint16_t c = p < 33 ? view_themes[theme][p] : 0xF0F;   /* (magenta: no such pen) */
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
    static const VoxBand bands[2] = { { 70 * 16, { PEN_TRACK, PEN_TRACK + 1 }, 1 }, { 78 * 16, { PEN_WHITE, PEN_KERB }, 0 } };
    rr_new_game(&g, seed, track);
    cam_yaw = g.yaw;
    vox_host_fb = fb;
    vox_world(GROUND_SHIFT, 2);
    vox_maps(ground_h, ground_pen, 2);
    vox_ribbon(ground_mid, ground_stripe, bands, 2);
    vox_lens(168, 79, 0, 96);
    vox_depth(24, 800, 8);
    ground_reset(&g, g.x >> 16, g.z >> 16);
    for (a = 3; a < argc; a++) {
        int want = atoi(argv[a]);
        char path[64];
        int32_t fx, fz, cx, cy, cz, gy;
        for (; t < want && !g.finished && !g.over; t++) {
            rr_tick(&g, rr_bot(&g));
            cam_yaw = (cam_yaw + (int32_t)(int16_t)(g.yaw - cam_yaw) / 4) & 0xFFFF;
        }
        /* the original's chase camera */
        fx = a3_sin((uint16_t)cam_yaw); fz = a3_cos((uint16_t)cam_yaw);
        cx = (g.x - fx * 150) >> 16; cz = (g.z - fz * 150) >> 16; cy = (g.y >> 16) + 62;
        gy = rr_height(&g, cx, cz) + 30;
        if (cy < gy) cy = gy;
        ground_reset(&g, cx, cz);
        vox_camera(cx, cy, cz, (uint16_t)cam_yaw);
        /* the horizon: the original's (cyc - fov * sin(pitch) / cos(pitch)) at 3/4 height, halved */
        vox_lens(168, 79, (78 - (int16_t)((158L * (a3_sin(1500) >> 4)) / (a3_cos(1500) >> 4))) / 2, 96);
        memset(fb, 0, sizeof(fb));
        printf("slices %d ", vox_draw());
        vox_sky(PEN_SKY);
        snprintf(path, sizeof(path), "build/host/vox_%d.ppm", want);
        save(path, view_theme(g.track));
        printf("%s z %ld\n", path, (long)(g.z >> 16));
    }
    return 0;
}
