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

    out = fopen("src/logic_tab.c", "w");
    if (!out) { perror("src/logic_tab.c"); return 1; }
    fprintf(out, "/* made by tools/gen_data.c from the original's maze.c: do not edit\n"
                 "   the grid game.c reads, in bank 17 (0x4000, while the logic runs): [cell * 4 + exit] */\n"
                 "#include <stdint.h>\n#include \"pc.h\"\n\n");
    fprintf(out, "const uint16_t mz_nb[CELLS * 4] = {\n");
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
    fprintf(out, "};\n");
    fclose(out);

    out = fopen("src/maze_tab.c", "w");
    fprintf(out, "/* made by tools/gen_data.c: do not edit */\n#include <stdint.h>\n#include \"pc.h\"\n\n"
                 "/* the keys' and the nest's directions, Q14 (main memory, for the view) */\nconst vec3 mz_spot[5] = {\n");
    for (k = 0; k < 4; k++) fprintf(out, "    { %d, %d, %d },\n", mz_dir[mz_keys[k]].x, mz_dir[mz_keys[k]].y, mz_dir[mz_keys[k]].z);
    fprintf(out, "    { %d, %d, %d }\n};\n", mz_dir[mz_nest].x, mz_dir[mz_nest].y, mz_dir[mz_nest].z);
    fclose(out);

    out = fopen("src/rtab_b.c", "w");
    fprintf(out, "/* made by tools/gen_data.c: do not edit\n"
                 "   the renderer's wall tables, page 40 at 0x6000 while walls are listed */\n"
                 "#include <stdint.h>\n#include \"rtab.h\"\n\n");
    fprintf(out, "/* each edge's wall (edges: (cell, exit) with neighbour > cell, in order):\n"
                 "   its corners' records, its side normal (Q7) */\nconst Edge ed_rec[EDGES] = {\n");
    for (i = 0; i < e; i++)
        fprintf(out, "    { CREC(%d), CREC(%d), { %d, %d, %d }, 0 },\n", ed_a[i], ed_b[i],
                q7(ed_side[i].x), q7(ed_side[i].y), q7(ed_side[i].z));
    fprintf(out, "};\n");
    fclose(out);

    out = fopen("src/rtab_a.c", "w");
    fprintf(out, "/* made by tools/gen_data.c: do not edit\n"
                 "   the renderer's corner and cell tables, page 39 at 0x6000 */\n"
                 "#include <stdint.h>\n#include \"rtab.h\"\n\n");
    fprintf(out, "/* lattice corners' directions, Q7 (128 = 1.0) + 128 (offset binary: xform.asm's\n"
                 "   products are unsigned) */\nconst uint8_t rd_corner8[CORNERS * 3] = {\n");
    for (i = 0; i < ncorner; i++) {
        V3 v = mz_lattice_dir(corner_lat[i][0], corner_lat[i][1], corner_lat[i][2]);
        item("%d,", q7(v.x) + 128); item("%d,", q7(v.y) + 128); item("%d,", q7(v.z) + 128);
    }
    end_array();
    fprintf(out, "/* each cell's four lattice corners (-a-b, +a-b, +a+b, -a+b) */\nconst uint16_t rd_cell_corner[CELLS * 4] = {\n");
    for (c = 0; c < CELLS; c++) {
        int a[3], b[3], sa, sb;
        face_axes(mz_face[c], a, b);
        for (k = 0; k < 4; k++) {
            sa = (k == 1 || k == 2) ? 1 : -1;
            sb = k >= 2 ? 1 : -1;
            i = corner_index(mz_lat[c][0] + sa * a[0] + sb * b[0], mz_lat[c][1] + sa * a[1] + sb * b[1],
                             mz_lat[c][2] + sa * a[2] + sb * b[2]);
            if (i >= 6 * GRID_N * GRID_N + 2) { fprintf(stderr, "cell %d: corner not on an edge\n", c); return 1; }
            fprintf(out, "%sCREC(%d),", k ? "" : "    ", i);
        }
        fprintf(out, "\n");
    }
    fprintf(out, "};\n");
    fclose(out);

    /* rtab.c: the nest's rim, the projection's scales (bank 18, with the renderer) */
    out = fopen("src/rtab.c", "w");
    fprintf(out, "/* made by tools/gen_data.c: do not edit */\n#include <stdint.h>\n#include \"rtab.h\"\n\n");
    {
        static const int cs[8][2] = { {16384,0},{11585,11585},{0,16384},{-11585,11585},
            {-16384,0},{-11585,-11585},{0,-16384},{11585,-11585} };
        V3 n = mz_dir[mz_nest], t1 = mz_tan[mz_nest][0], t2 = mz_tan[mz_nest][1];
        V3 ctr = v3_scale14(n, PLANET_R + Q8(0.02));
        fprintf(out, "/* the nest's rim (the original's render_cel.c), world Q8 */\nconst V3 rd_nest_rim[8] = {\n");
        for (k = 0; k < 8; k++) {
            V3 p = v3_add(ctr, v3_add(v3_scale14(t1, (Q8(1.25) * cs[k][0]) >> 14), v3_scale14(t2, (Q8(1.25) * cs[k][1]) >> 14)));
            fprintf(out, "    { %d, %d, %d },\n", p.x, p.y, p.z);
        }
        fprintf(out, "};\n");
    }
    fprintf(out, "/* 196.8 (the original's focal length, 246, at 0.8) * 16384 / z */\nconst int16_t rd_recip[RECIP_N] = {\n");
    for (i = 0; i < 1024; i++) {
        double v = 196.8 * 16384.0 / (i * 16 + 8);
        item("%d,", v > 32767 ? 32767 : (int)(v + 0.5));
    }
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
