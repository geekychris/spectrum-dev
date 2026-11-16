/*
 * hoststep.c - the port's simulation (world.c, world_setup.c, terrain.c,
 * ts_math.c) built for the host, as a step server for test.py's lockstep
 * check: it reads a World (the Next's bytes: ts.h packs the struct on the
 * host, so the layouts match) and the input of one world_update() on stdin,
 * runs the update and writes the World back. The island's heights are the
 * original's terrain_generate(1987), as tools/gen_terrain.c makes them for
 * the Next.
 *
 *   cc -Isrc tools/hoststep.c src/world.c src/world_setup.c src/terrain.c src/ts_math.c -o build-trace/hoststep
 */
#include <stdio.h>
#include <string.h>
#include "ts.h"
#include "island.h"

int main(void)
{
    static World w, tmp;
    unsigned char in[2];
    terrain_generate(&tmp, ISLAND_SEED);
    setvbuf(stdout, NULL, _IOFBF, sizeof(World));
    for (;;) {
        if (fread(&w, sizeof w, 1, stdin) != 1) return 0;
        if (fread(in, 2, 1, stdin) != 1) return 0;
        world_update(&w, in[0] | in[1] << 8);
        fwrite(&w, sizeof w, 1, stdout);
        fflush(stdout);
    }
}
