/*
 * gen_data.c - the sphere's static tables for the Next, made by the
 * original's own maze.c (tools/orig) with 32-bit longs, as on the 3DO and
 * the Amiga:
 *
 *   src/maze_tab.c   the grid (neighbours, reverse exits, directions,
 *                    tangents, edge lengths: what game.c and maze.c use),
 *                    and the renderer's tables: cell and lattice-corner
 *                    directions in Q7, each edge's corners, side normal and
 *                    midpoint
 *   src/maze_tab.h   their sizes and the special cells
 *   build/level1.txt the original's level-1 maze (test.py compares the
 *                    port's carving with it)
 *
 * The maze itself (which edges are open) is carved on the Next at the
 * start of each level by the original's algorithm (src/maze.c).
 *
 * cc -O2 -w -Itools/orig tools/gen_data.c -o build/gen_data && build/gen_data
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define long int                    /* the original's longs are 32 bits */
#include "../tools/orig/maze.c"
#undef long

static int q7(long v)               /* Q14 -> Q7 (128 = 1.0), rounded, clamped */
{
    double d = v / 128.0;
    int r = (int)floor(d + 0.5);
    return r > 127 ? 127 : r < -128 ? -128 : r;
}

#define MAXC 600
static int corner_lat[MAXC][3], ncorner;

static int corner_index(int x, int y, int z)
{
    int i;
    for (i = 0; i < ncorner; i++)
        if (corner_lat[i][0] == x && corner_lat[i][1] == y && corner_lat[i][2] == z) return i;
    corner_lat[ncorner][0] = x; corner_lat[ncorner][1] = y; corner_lat[ncorner][2] = z;
    return ncorner++;
}

static FILE *out;
static int col;
static void item(const char *fmt, int v)
{
    if (col == 0) fprintf(out, "    ");
    fprintf(out, fmt, v);
    if (++col == 16) { fprintf(out, "\n"); col = 0; }
}
static void end_array(void) { if (col) fprintf(out, "\n"); col = 0; fprintf(out, "};\n"); }

int main(void)
{
    int c, k, i, d, e = 0;
    static int ed_cell[CELLS * 2], ed_k[CELLS * 2], ed_a[CELLS * 2], ed_b[CELLS * 2], ed_of[CELLS][4];
    static V3 ed_side[CELLS * 2], ed_mid[CELLS * 2];
    FILE *h, *lv;

    mz_build(2026UL + 7919UL * 1);          /* level 1, as build_level() */
    mz_build_walls();

    /* edges: each (c, k) with neighbour > c, in the original's wall order */
    for (c = 0; c < CELLS; c++)
        for (k = 0; k < 4; k++) {
            int mid[3], ee[3], *n = mz_face[c], *dd = mz_step[c][k];
            V3 da, db;
            ed_of[c][k] = -1;
            if (mz_nb[c][k] < c) continue;
            for (i = 0; i < 3; i++) mid[i] = mz_lat[c][i] + dd[i];
            ee[0] = n[1] * dd[2] - n[2] * dd[1];
            ee[1] = n[2] * dd[0] - n[0] * dd[2];
            ee[2] = n[0] * dd[1] - n[1] * dd[0];
            ed_cell[e] = c;
            ed_k[e] = k;
            ed_a[e] = corner_index(mid[0] - ee[0], mid[1] - ee[1], mid[2] - ee[2]);
            ed_b[e] = corner_index(mid[0] + ee[0], mid[1] + ee[1], mid[2] + ee[2]);
            da = mz_lattice_dir(mid[0] - ee[0], mid[1] - ee[1], mid[2] - ee[2]);
            db = mz_lattice_dir(mid[0] + ee[0], mid[1] + ee[1], mid[2] + ee[2]);
            ed_mid[e] = v3_norm14(v3_add(da, db));
            ed_side[e] = v3_norm14(v3_cross14(ed_mid[e], v3_sub(db, da)));
            ed_of[c][k] = e;
            e++;
        }
    for (c = 0; c < CELLS; c++)               /* the other side of each edge */
        for (k = 0; k < 4; k++)
            if (ed_of[c][k] < 0) ed_of[c][k] = ed_of[mz_nb[c][k]][mz_rev[c][k]];
    if (e != CELLS * 2 || ncorner != 6 * GRID_N * GRID_N + 2) {
        fprintf(stderr, "unexpected: %d edges, %d corners\n", e, ncorner);
        return 1;
    }

    out = fopen("src/maze_tab.c", "w");
    if (!out) { perror("src/maze_tab.c"); return 1; }
    fprintf(out, "/* made by tools/gen_data.c from the original's maze.c: do not edit */\n"
                 "#include <stdint.h>\n#include \"pc.h\"\n\n");
    fprintf(out, "/* the grid: [cell * 4 + exit] */\nconst uint16_t mz_nb[CELLS * 4] = {\n");
    for (c = 0; c < CELLS; c++) for (k = 0; k < 4; k++) item("%d,", mz_nb[c][k]);
    end_array();
    fprintf(out, "const uint8_t mz_rev[CELLS * 4] = {\n");
    for (c = 0; c < CELLS; c++) for (k = 0; k < 4; k++) item("%d,", mz_rev[c][k]);
    end_array();
    fprintf(out, "/* edge lengths, world Q8, minus MZ_LEN_BASE */\nconst uint8_t mz_len8[CELLS * 4] = {\n");
    for (c = 0; c < CELLS; c++) for (k = 0; k < 4; k++) {
        int v = mz_len[c][k] - 400;
        if (v < 0 || v > 255) { fprintf(stderr, "edge length %d\n", mz_len[c][k]); return 1; }
        item("%d,", v);
    }
    end_array();
    fprintf(out, "/* directions, Q14 */\nconst vec3 mz_dir[CELLS] = {\n");
    for (c = 0; c < CELLS; c++)
        fprintf(out, "    { %d, %d, %d },\n", mz_dir[c].x, mz_dir[c].y, mz_dir[c].z);
    fprintf(out, "};\n/* surface tangents toward each neighbour, Q14 */\nconst vec3 mz_tan[CELLS * 4] = {\n");
    for (c = 0; c < CELLS; c++)
        for (k = 0; k < 4; k++)
            fprintf(out, "    { %d, %d, %d },\n", mz_tan[c][k].x, mz_tan[c][k].y, mz_tan[c][k].z);
    fprintf(out, "};\n\n/* the renderer's: directions in Q7 (128 = 1.0) */\nconst int8_t rd_cell8[CELLS * 3] = {\n");
    for (c = 0; c < CELLS; c++) { item("%d,", q7(mz_dir[c].x)); item("%d,", q7(mz_dir[c].y)); item("%d,", q7(mz_dir[c].z)); }
    end_array();
    fprintf(out, "const int8_t rd_corner8[CORNERS * 3] = {\n");
    for (i = 0; i < ncorner; i++) {
        V3 v = mz_lattice_dir(corner_lat[i][0], corner_lat[i][1], corner_lat[i][2]);
        item("%d,", q7(v.x)); item("%d,", q7(v.y)); item("%d,", q7(v.z));
    }
    end_array();
    fprintf(out, "/* edges (cell, exit) with neighbour > cell: the cell * 4 + exit */\nconst uint16_t ed_ck[EDGES] = {\n");
    for (i = 0; i < e; i++) item("%d,", ed_cell[i] * 4 + ed_k[i]);
    end_array();
    fprintf(out, "/* the edge of each (cell, exit) */\nconst uint16_t ed_of[CELLS * 4] = {\n");
    for (c = 0; c < CELLS; c++) for (k = 0; k < 4; k++) item("%d,", ed_of[c][k]);
    end_array();
    fprintf(out, "/* each edge's wall: corners A, B */\nconst uint16_t ed_corner[EDGES * 2] = {\n");
    for (i = 0; i < e; i++) { item("%d,", ed_a[i]); item("%d,", ed_b[i]); }
    end_array();
    fprintf(out, "/* the wall's side normal and midpoint direction, Q7 */\nconst int8_t ed_side8[EDGES * 3] = {\n");
    for (i = 0; i < e; i++) { item("%d,", q7(ed_side[i].x)); item("%d,", q7(ed_side[i].y)); item("%d,", q7(ed_side[i].z)); }
    end_array();
    fprintf(out, "const int8_t ed_mid8[EDGES * 3] = {\n");
    for (i = 0; i < e; i++) { item("%d,", q7(ed_mid[i].x)); item("%d,", q7(ed_mid[i].y)); item("%d,", q7(ed_mid[i].z)); }
    end_array();
    fclose(out);

    h = fopen("src/maze_tab.h", "w");
    fprintf(h, "/* made by tools/gen_data.c: do not edit */\n"
               "#define CORNERS %d\n#define EDGES %d\n#define MZ_START %d\n#define MZ_NEST %d\n"
               "#define MZ_KEY0 %d\n#define MZ_KEY1 %d\n#define MZ_KEY2 %d\n#define MZ_KEY3 %d\n"
               "#define MZ_CELL_LEN %d\n#define MZ_LEN_BASE 400\n",
            ncorner, e, mz_start, mz_nest, mz_keys[0], mz_keys[1], mz_keys[2], mz_keys[3], mz_cell_len);
    fclose(h);

    /* the original's level 1: open exits per cell as a hex digit, and its walls */
    lv = fopen("build/level1.txt", "w");
    for (c = 0; c < CELLS; c++)
        fprintf(lv, "%x", mz_open[c][0] | mz_open[c][1] << 1 | mz_open[c][2] << 2 | mz_open[c][3] << 3);
    fprintf(lv, "\n%d\n", mz_nwalls);
    fclose(lv);
    printf("gen_data: %d cells, %d corners, %d edges, level 1: %d walls\n", CELLS, ncorner, e, mz_nwalls);
    return 0;
}
