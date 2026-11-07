/*
 * gen_models.c - the original's polygon models (src/models.c, built by its
 * models_init()) as compact constant tables for the Next: src/models_data.c.
 * The original fills fixed-size arrays (32 vertices, 24 faces a model) in
 * code at start-up; here only the vertices and faces used are kept.
 *
 *   cc -Isrc tools/gen_models.c src/models.c -o build/gen_models && build/gen_models
 */
#include <stdio.h>
#include "models.h"

int main(void)
{
    FILE *f = fopen("src/models_data.c", "w");
    int m, i, k, nv = 0, nf = 0;
    models_init();
    fprintf(f, "/* made by tools/gen_models.c from the original's models.c - do not edit */\n");
    fprintf(f, "#include <stdint.h>\n#include \"models_data.h\"\n\n");
    fprintf(f, "const int16_t mdl_verts[][3] = {\n");
    for (m = 0; m < MDL_COUNT; m++)
        for (i = 0; i < models[m].nv; i++)
            fprintf(f, "    { %d, %d, %d },\n", models[m].v[i][0], models[m].v[i][1], models[m].v[i][2]);
    fprintf(f, "};\n\nconst MFace mdl_faces[] = {\n");
    for (m = 0; m < MDL_COUNT; m++)
        for (k = 0; k < models[m].nf; k++) {
            const Face *fc = &models[m].f[k];
            fprintf(f, "    { %d, %d, { %d, %d, %d, %d } },\n", fc->n, fc->col, fc->idx[0], fc->idx[1], fc->idx[2],
                    fc->n == 4 ? fc->idx[3] : 0);
        }
    fprintf(f, "};\n\nconst MModel mdl_models[%d] = {\n", MDL_COUNT);
    for (m = 0; m < MDL_COUNT; m++) {
        fprintf(f, "    { %d, %d, %d, %d },\n", models[m].nv, models[m].nf, nv, nf);
        nv += models[m].nv;
        nf += models[m].nf;
    }
    fprintf(f, "};\n");
    fclose(f);
    printf("%d models: %d vertices, %d faces (%d bytes)\n", MDL_COUNT, nv, nf, nv * 6 + nf * 6 + MDL_COUNT * 6);
    return 0;
}
