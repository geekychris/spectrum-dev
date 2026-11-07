/* Bullion Dash - the level editor (the original's editor.h) */
#ifndef EDITOR_H
#define EDITOR_H

#include <stdint.h>

#define EDITOR_TEST 1           /* play the level (gs is set up for it) */
#define EDITOR_EXIT 2           /* back to the title */

void editor_init(void);         /* a blank level (solid bottom row) */
void editor_resume(void);       /* after a test play: the level as it was */
uint8_t editor_update(uint8_t held, uint8_t pressed, char letter);
void editor_draw(void);

#endif
