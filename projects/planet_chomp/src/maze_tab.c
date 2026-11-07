/* made by tools/gen_data.c: do not edit */
#include <stdint.h>
#include "pc.h"

/* the keys' and the nest's directions, Q14 (main memory, for the view) */
const vec3 mz_spot[5] = {
    { 16384, 0, 0 },
    { 0, 0, 16384 },
    { -16384, 0, 0 },
    { 0, 0, -16384 },
    { 0, -16384, 0 }
};
