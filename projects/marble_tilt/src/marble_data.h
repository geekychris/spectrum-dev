/* made by tools/gen_view.py: the marble sprite's pictures and the camera */
#ifndef MARBLE_DATA_H
#define MARBLE_DATA_H
#include <stdint.h>
#define MARBLE_SIZES  10             /* radius in half pixels */
#define MARBLE_SHADES 8
#define MARBLE_PAL    240           /* sprite palette entries of the shades */
#define MARBLE_FOV    233
#define MARBLE_CX     128
#define MARBLE_CY     60
#define MARBLE_CCP    48891L        /* the camera's pitch: cos, sin (16.16) */
#define MARBLE_CSP    43634L
extern const int8_t *const marble_images[MARBLE_SIZES];   /* dx, dy, shade ... 127 */
extern const int8_t marble_shadow[];                      /* dx, dy ... 127 */
extern const uint16_t marble_shades[MARBLE_SHADES];
#endif
