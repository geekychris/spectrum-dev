/* Terra Strike - the original's polygon models as constant tables
   (tools/gen_models.c makes models_data.c from models.c) */
#ifndef MODELS_DATA_H
#define MODELS_DATA_H

#include <stdint.h>

typedef struct { uint8_t n, col; uint8_t idx[4]; } MFace;
typedef struct { uint8_t nv, nf; uint16_t v0, f0; } MModel;   /* first vertex, face */

extern const int16_t mdl_verts[][3];
extern const MFace mdl_faces[];
extern const MModel mdl_models[];

#endif
