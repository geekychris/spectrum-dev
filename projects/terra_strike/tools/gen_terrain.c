/*
 * gen_terrain.c - the island the game flies over, made on the host with the
 * original's terrain_generate() (src/terrain.c, seed 1987 as the original's
 * game.c), written for the Next:
 *
 *   src/island.asm   the 64 x 64 heights (int16, little-endian: 8K) in
 *                    16K bank ISLAND_BANK's first page, loaded by the .nex
 *                    and mapped into MMU slot 3 (0x6000) as TS_HEIGHTS
 *   src/island.h     the seed, the landing pad, the page
 *
 *   cc -Isrc tools/gen_terrain.c src/terrain.c src/ts_math.c -o build/gen_terrain
 *   build/gen_terrain     (tools/build_data.sh does both)
 */
#include <stdio.h>
#include "ts.h"

#define SEED 1987
#define ISLAND_BANK 30

int main(void)
{
    static World w;
    FILE *f;
    int i;

    terrain_generate(&w, SEED);
    f = fopen("src/island.asm", "w");
    fprintf(f, "; made by tools/gen_terrain.c: the original's terrain_generate(%d), 64 x 64 heights\n", SEED);
    fprintf(f, "SECTION BANK_%d\n", ISLAND_BANK);
    for (i = 0; i < MAP_N * MAP_N; i += 16) {
        int k;
        fprintf(f, "    defw ");
        for (k = 0; k < 16; k++) fprintf(f, "%d%s", ts_heights[i + k], k < 15 ? "," : "\n");
    }
    fclose(f);
    f = fopen("src/island.h", "w");
    fprintf(f, "/* made by tools/gen_terrain.c */\n#ifndef ISLAND_H\n#define ISLAND_H\n");
    fprintf(f, "#define ISLAND_SEED %d\n#define ISLAND_PAD_TX %d\n#define ISLAND_PAD_TZ %d\n#define ISLAND_PAD_H %ld\n",
            SEED, w.pad_tx, w.pad_tz, (long)w.pad_h);
    fprintf(f, "#define ISLAND_PAGE %d      /* 8K page: the heights (bank %d's first half) */\n", 2 * ISLAND_BANK, ISLAND_BANK);
    fprintf(f, "#endif\n");
    fclose(f);
    printf("island seed %d: pad at tile %d,%d height %ld\n", SEED, w.pad_tx, w.pad_tz, (long)w.pad_h);
    return 0;
}
