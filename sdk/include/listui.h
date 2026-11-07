/*
 * listui.h - a scrolling list of items on Layer 2's 320 x 256 mode (l2w_*):
 * one ROM-font line per row, the selected row on a bar, an optional tag
 * (e.g. "48K" / "NEXT") right-aligned in its own colour, and scroll marks
 * when there are more items above or below. The program keeps the items;
 * listui asks for each row's text through callbacks.
 *
 *   listui l = { .x = 16, .y = 48, .w = 128, .rows = 12, .row_h = 12,
 *                .count = n, .bg = 0, .fg = 0xFF, .bar = 0x03, .bar_fg = 0xFF,
 *                .label = name_of, .tag = tag_of };
 *   listui_draw(&l);                 everything
 *   listui_move(&l, +1);             select the next item (clamped), scrolling
 *                                    and redrawing only what changed; 1 if it moved
 *   listui_select(&l, i);            select item i (scrolls it into view)
 */
#ifndef ZX_LISTUI_H
#define ZX_LISTUI_H
#include <stdint.h>

typedef struct listui {
    uint16_t x, w;                   /* left edge, width in pixels */
    uint8_t y, rows, row_h;          /* top, visible rows, row height (>= 10) */
    uint8_t count, top, sel;         /* items, first visible, selected */
    uint8_t bg, fg, bar, bar_fg;     /* colours (RRRGGGBB; avoid 0xE3) */
    const char *(*label)(uint8_t i);                /* text of item i */
    const char *(*tag)(uint8_t i, uint8_t *colour); /* optional tag + its colour, or 0 */
} listui;

extern void listui_draw(listui *l);
extern void listui_draw_row(listui *l, uint8_t i);  /* item i, if visible */
extern uint8_t listui_move(listui *l, int8_t delta);
extern void listui_select(listui *l, uint8_t i);

#endif
