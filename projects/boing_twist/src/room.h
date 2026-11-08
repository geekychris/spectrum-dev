/* room.c - the room: unpacked into ROOM_BANK, copied to the back buffer, the shadow */
#ifndef ROOM_H
#define ROOM_H
#include <stdint.h>
#include "gfx_data.h"

extern uint8_t room_cur;
void room_load(uint8_t r);
void room_to_back(void);
/* the ball's shadow in the back buffer: on the wall (a disc at wx, wy) or the
   floor (an ellipse at fx, fy), as the room has it */
void room_shadow(int16_t wx, int16_t wy, int16_t fx, int16_t fy, uint8_t rx, uint8_t ry);
#endif
