/* l2ring.h: scrolling (the drawing is l2ring.asm) */
#include <string.h>
#include <arch/zxn.h>
#include "l2ring.h"

int16_t lr_x, lr_y;
uint16_t lr_cols, lr_rows;
static uint8_t whole = 1;                /* the next goto draws everything */

static uint16_t ring_col(int16_t x)      /* Layer 2 column of picture column x */
{
    int16_t c = x % 320;
    return (uint16_t)(c < 0 ? c + 320 : c);
}

void lr_redraw(void) { whole = 1; }

void lr_goto(int16_t nx, int16_t ny)
{
    int16_t dx = nx - lr_x, dy = ny - lr_y, x;
    uint16_t c;
    if (whole || dx > 64 || dx < -64 || dy > 64 || dy < -64) {
        whole = 0;
        lr_x = nx;
        lr_y = ny;
        for (x = nx; x < nx + 320; x++) lr_col(ring_col(x), x, ny);
        lr_cols += 320;
    } else {
        /* new columns first (at the old rows), then new rows across the new columns */
        if (dx > 0)
            for (x = lr_x + 320; x < nx + 320; x++) lr_col(ring_col(x), x, lr_y);
        else
            for (x = nx; x < lr_x; x++) lr_col(ring_col(x), x, lr_y);
        lr_cols += dx < 0 ? -dx : dx;
        lr_x = nx;
        if (dy > 0)
            for (x = lr_y + 256; x < ny + 256; x++) lr_row(ring_col(nx), nx, x);
        else
            for (x = ny; x < lr_y; x++) lr_row(ring_col(nx), nx, x);
        lr_rows += dy < 0 ? -dy : dy;
        lr_y = ny;
    }
    c = ring_col(lr_x);
    ZXN_WRITE_REG(0x16, c & 255);
    ZXN_WRITE_REG(0x71, c >> 8);
    ZXN_WRITE_REG(0x17, (uint8_t)lr_y);
}

uint8_t lr_palette(uint16_t *dst, uint8_t max)
{
    uint8_t n, keep = ZXN_READ_REG(0x52);
    ZXN_WRITE_MMU2(lr_page);
    n = *(const uint8_t *)0x4009;
    if (n > max) n = max;
    memcpy(dst, (const void *)0x400A, n * 2);
    ZXN_WRITE_REG(0x52, keep);
    return n;
}
