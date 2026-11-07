/* A scrolling list on Layer 2's 320 x 256 mode (see listui.h). */
#include <stdint.h>
#include <string.h>
#include "nextgfx.h"
#include "listui.h"

#define MARK_W 8

static void marks(listui *l)
{
    uint16_t mx = l->x + l->w - MARK_W + 1;
    uint8_t bottom = l->y + l->rows * l->row_h - 3;
    l2w_fill(mx, l->y - 4, 6, 3, l->top ? l->fg : l->bg);
    l2w_fill(mx, bottom + 4, 6, 3, l->top + l->rows < l->count ? l->fg : l->bg);
}

void listui_draw_row(listui *l, uint8_t i)
{
    uint8_t row, y, sel, bg, fg, tc;
    const char *t;
    if (i < l->top || i >= l->top + l->rows) return;
    row = i - l->top;
    y = l->y + row * l->row_h;
    sel = i == l->sel;
    bg = sel ? l->bar : l->bg;
    fg = sel ? l->bar_fg : l->fg;
    l2w_fill(l->x, y, l->w, l->row_h, bg);
    if (i >= l->count) return;
    l2w_text(l->x + 4, y + (l->row_h - 8) / 2, l->label(i), fg, 1);
    if (l->tag && (t = l->tag(i, &tc)) != 0)
        l2w_text(l->x + l->w - 4 - L2W_TEXT_W(strlen(t), 1), y + (l->row_h - 8) / 2, t, sel ? fg : tc, 1);
}

void listui_draw(listui *l)
{
    uint8_t r;
    for (r = 0; r < l->rows; r++) listui_draw_row(l, l->top + r);
    marks(l);
}

void listui_select(listui *l, uint8_t i)
{
    uint8_t old = l->sel, top = l->top;
    if (!l->count) return;
    if (i >= l->count) i = l->count - 1;
    l->sel = i;
    if (i < l->top) l->top = i;
    else if (i >= l->top + l->rows) l->top = i - l->rows + 1;
    if (l->top != top) listui_draw(l);
    else { listui_draw_row(l, old); listui_draw_row(l, i); }
}

uint8_t listui_move(listui *l, int8_t delta)
{
    int16_t i = (int16_t)l->sel + delta;
    if (i < 0) i = 0;
    if (i >= l->count) i = l->count - 1;
    if (i < 0 || (uint8_t)i == l->sel) return 0;
    listui_select(l, (uint8_t)i);
    return 1;
}
