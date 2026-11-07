/* the original's models (src/models.c, from tools/gen_models.py) */
#ifndef MODELS_H
#define MODELS_H
#include <stdint.h>

#define MD_BODY 200                     /* a face in the body colours given when drawing (+1 shaded) */
typedef struct { uint8_t n, idx[4], col; } md_face;
typedef struct { uint8_t nv, nf; const int16_t (*v)[3]; const md_face *f; } md_model;
enum { MD_PILLAR, MD_PYRAMID, MD_CRATE, MD_SCOUT, MD_MORTAR, MD_BULWARK, MD_COUNT };
extern const md_model md_models[MD_COUNT];

#endif
