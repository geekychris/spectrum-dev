/*
 * gen_sprites.c - the original's sprite textures (its own sprites.c: the
 * chomper in 4 facings x 3 mouths, normal and powered; the spooks; the
 * key), 32 x 32 in 15-bit RGB (0 = transparent), written to
 * build/textures.bin in the order of sprites.h for gen_gfx.py.
 *
 * cc -O2 -w -Itools/stub -Itools/orig tools/gen_sprites.c -o build/gen_sprites && build/gen_sprites
 */
#include <stdio.h>
#include <string.h>
#include "pc.h"
#include "sprites.h"

static unsigned short tex[64][32 * 32];
static int ntex;

int pc_tex_create(int w, int h, unsigned short *pixels)
{
    if (w != 32 || h != 32 || ntex >= 64) return -1;
    memcpy(tex[ntex], pixels, sizeof(tex[0]));
    return ntex++;
}

#include "sprites.c"

int main(void)
{
    FILE *f;
    int i, pw, d, m;
    if (!sprites_init()) { fprintf(stderr, "sprites_init failed\n"); return 1; }
    f = fopen("build/textures.bin", "wb");
    /* chomper[powered][dir][mouth], then the spooks (GT_*), then the key */
    for (pw = 0; pw < 2; pw++)
        for (d = 0; d < 4; d++)
            for (m = 0; m < 3; m++) fwrite(tex[tex_chomper[pw][d][m]], 2, 32 * 32, f);
    for (i = 0; i < GT_COUNT; i++) fwrite(tex[tex_ghost[i]], 2, 32 * 32, f);
    fwrite(tex[tex_key], 2, 32 * 32, f);
    fclose(f);
    printf("gen_sprites: %d textures\n", 24 + GT_COUNT + 1);
    return 0;
}
