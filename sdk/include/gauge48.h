/*
 * gauge48.h - a horizontal bar gauge on the 48K screen (fuel, energy,
 * shields): a frame `cells` characters wide on one character row, and a bar
 * inside it (pixel lines 2..5 of the row, up to cells * 8 - 2 pixels long)
 * whose length and colour are set each frame. Only the pixels between the
 * old and the new length are redrawn, and the attributes only when the
 * colour changes, so calling gauge_set every frame costs nothing when
 * nothing changed.
 */
#ifndef ZX_GAUGE48_H
#define ZX_GAUGE48_H
#include <stdint.h>
typedef struct {
    uint8_t col, row, cells;    /* where */
    uint8_t len, attr;          /* what is shown: bar length in pixels, colour */
} gauge;
#define GAUGE_MAX(g) ((uint8_t)((g)->cells * 8 - 2))
/* draw the frame (an empty bar) in colour attr */
extern void gauge_init(gauge *g, uint8_t col, uint8_t row, uint8_t cells, uint8_t attr);
/* bar length len (clipped to GAUGE_MAX) in colour attr */
extern void gauge_set(gauge *g, uint8_t len, uint8_t attr);
/* len for value / max of the bar (0 when max is 0) */
extern uint8_t gauge_len(const gauge *g, uint16_t value, uint16_t max);
#endif
