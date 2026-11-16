/* voxeltest - the SDK's voxel terrain (voxel.h) on the Next: three views of
   a landscape drawn by voxel_draw.asm, each logged with a checksum of the
   rows drawn (test.py compares them with the host's C reference) and the
   time taken. */
#include <stdint.h>
#include <arch/zxn.h>
#include <input.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "zxlog.h"
#include "voxel.h"
#include "scene.h"

static uint16_t sum_rows(void)
{
    uint16_t s = 0, i;
    uint8_t y, saved = ZXN_READ_REG(0x53);
    for (y = 0; y < 96; y++) {
        const uint8_t *p;
        ZXN_WRITE_REG(0x53, l2_back * 2 + (y >> 5));
        p = (const uint8_t *)0x6000 + ((uint16_t)(y & 31) << 8);
        for (i = 0; i < 256; i++) s = s * 31 + p[i];
    }
    ZXN_WRITE_REG(0x53, saved);
    return s;
}

static uint16_t raster(void) { return ((uint16_t)(ZXN_READ_REG(0x1E) & 1) << 8) | ZXN_READ_REG(0x1F); }

int main(void)
{
    uint8_t k, n;
    next_init();
    l2_init();
    frame_init();
    scene_make();
    zx_log("vox: ready\n");
    for (k = 0; k < SCENE_VIEWS; k++) {
        uint16_t t0, t1;
        uint8_t f0;
        scene_view(k);
        l2_clear(0);
        frame_wait();
        f0 = frame_ticks;
        t0 = raster();
        n = vox_draw();
        t1 = raster();
        /* lines: frames since f0 * 312 + raster difference */
        zx_log("vox: view %u slices %u sum %u lines %u\n", k, n, sum_rows(),
               (uint16_t)((uint8_t)(frame_ticks - f0) * 312 + t1 - t0));
        vox_sky(1);
        frame_wait();
        l2_flip();
        frame_wait();
        zx_log("vox: shown %u\n", k);
        while (!key_down(IN_KEY_SCANCODE_SPACE) && k + 1 < SCENE_VIEWS) frame_wait();
        while (key_down(IN_KEY_SCANCODE_SPACE)) frame_wait();
    }
    zx_log("vox: done\n");
    for (;;) frame_wait();
}
