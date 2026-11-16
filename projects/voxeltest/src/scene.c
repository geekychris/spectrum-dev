/* voxeltest's landscape: rolling hills from sines, a colour per 4 x 4 cells
   by height, a winding road as the ribbon; and three views of it */
#include "scene.h"
#include "fix16.h"

int8_t heights[128 * 128];
uint8_t colours[32 * 32];
static int16_t mid[128];
static uint8_t stripe[128];
static const VoxBand bands[2] = { { 3 * 256, { 9, 10 }, 1 }, { 3 * 256 + 96, { 11, 12 }, 0 } };

void scene_make(void)
{
    uint8_t x, z;
    for (z = 0; z < 128; z++) {
        for (x = 0; x < 128; x++) {
            int32_t h = (a3_sin((uint16_t)(x * 700)) >> 11) + (a3_sin((uint16_t)(z * 900 + x * 300)) >> 12);
            heights[(uint16_t)z * 128 + x] = (int8_t)h;
        }
        mid[z] = (int16_t)(64 * 256 + (a3_sin((uint16_t)(z * 600)) >> 5));
        stripe[z] = (z >> 2) & 1;
    }
    for (z = 0; z < 32; z++)
        for (x = 0; x < 32; x++) colours[z * 32 + x] = (uint8_t)(2 + ((heights[(uint16_t)z * 512 + x * 4] + 48) >> 4));
    vox_world(4, 2);                    /* cells of 16 units, heights * 4 */
    vox_maps(heights, colours, 2);
    vox_ribbon(mid, stripe, bands, 2);
    vox_depth(24, 900, 8);
}

void scene_view(uint8_t k)
{
    static const int16_t cx[3] = { 1024, 700, 1500 }, cz[3] = { 300, 900, 1500 };
    static const uint16_t yaw[3] = { 0, 6000, 40000 };
    vox_camera(cx[k], 120, cz[k], yaw[k]);
    vox_lens(168, 79, 20 + k * 6, 96);
}
