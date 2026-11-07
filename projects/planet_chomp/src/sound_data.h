/* made by tools/gen_sound.py - do not edit */
#ifndef SOUND_DATA_H
#define SOUND_DATA_H
#include <stdint.h>
/* a stream: 8K page, its address with the page in MMU slot 2 */
typedef struct { uint8_t page; uint16_t addr; } Stream;
extern const Stream title_streams[2];       /* chips 0, 1 */
extern const Stream sfx_streams[10];          /* game.h's SFX_* */
#endif
