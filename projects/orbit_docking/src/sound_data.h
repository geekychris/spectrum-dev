/* made by tools/gen_sound.py - do not edit */
#ifndef SOUND_DATA_H
#define SOUND_DATA_H
#include <stdint.h>
/* a stream: 8K page, address with the page in MMU slot 2 */
typedef struct { uint8_t page; uint16_t addr; } Stream;
extern const Stream music_streams[2];      /* chips 0 and 1 */
extern const Stream sfx_streams[7];       /* SFX_* (sound.h) */
#endif
