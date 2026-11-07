/* the original's models (src/models.c, from tools/gen_models.py) */
#ifndef MODELS_H
#define MODELS_H
#include <stdint.h>

#define MD_BODY 200                     /* a face in the body colours given when drawing (+1 shaded) */
/* a face: n vertices, their indices (wound outwards), its pen, and its
   outward plane: normal scaled to |64| on its largest axis, and d less the
   eye height's share, so the face looks at an eye (ex, 34, ez) in the
   model's frame when nx ex + nz ez > d */
typedef struct { uint8_t n, idx[4], col; int8_t nrm[3]; int16_t d; } md_face;
/* a vertex for the transform (xf.asm): x, z, and its height's share of the
   camera's y and z at the camera's pitch, (8 y cos p) >> 14, (8 y sin p) >> 14 */
typedef struct { int8_t x, z; int16_t ypc, ysp; } md_vtx;
typedef struct { uint8_t nv, nf; const int16_t (*v)[3]; const md_vtx *xv; const md_face *f; } md_model;
enum { MD_PILLAR, MD_PYRAMID, MD_CRATE, MD_SCOUT, MD_MORTAR, MD_BULWARK, MD_COUNT };
extern const md_model md_models[MD_COUNT];

#endif
