/*
 * room.c - the room round the ball (the original's themes and rd_set_theme).
 *
 * The original draws the room once, when it is chosen, into a background
 * picture, and each frame copies that and darkens the ball's shadow into
 * it. Here the rooms are drawn on the host from the original's box, its
 * grids and textures (tools/gen_data.py), in 16 colours at 4 bits a pixel;
 * choosing one unpacks it into banks 21-23 and loads its colours, each
 * with a shadowed copy 0x20 entries further on, so the shadow is an OR.
 */
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "zxgame.h"
#include "bt.h"
#include "room.h"

uint8_t room_cur;

static void set_mmu(uint8_t slot, uint8_t page) { ZXN_WRITE_REG(0x50 + slot, page); }

void room_load(uint8_t r)
{
    uint8_t y, i;
    uint16_t off = rooms[r].meta;
    uint8_t page = rooms[r].page;           /* (the table is in the logic bank, mapped out below) */
    const uint8_t *m;
    room_cur = r;
    set_mmu(0, page);
    set_mmu(1, page + 1);
    set_mmu(2, page + 2);
    ZXN_WRITE_REG(0x13, ROOM_BANK);
    y = 0;
    do {
        z80_outp(0x123B, 0x0B | (y & 0xC0));
        bt_unpack4((const uint8_t *)((uint16_t)y << 7), (uint8_t *)((uint16_t)(y & 63) << 8));
    } while (++y < 96);
    z80_outp(0x123B, 0x02);
    /* its 16 colours and their shadowed copies */
    set_mmu(2, META_PAGE + (off >> 13));
    set_mmu(3, META_PAGE + (off >> 13) + 1);
    m = (const uint8_t *)(0x4000 + (off & 0x1FFF));
    ZXN_WRITE_REG(0x43, 0x10);
    for (i = 0; i < 32; i++, m += 2) {
        uint8_t c = i >> 1, pen = ((c & 12) << 4) | 28 | (c & 3) | (i & 1 ? 0x20 : 0);
        ZXN_WRITE_REG(0x40, pen);
        ZXN_WRITE_REG(0x44, m[1] << 7 | m[0] >> 1);
        ZXN_WRITE_REG(0x44, m[0] & 1);
    }
    logic_map();
}

void room_to_back(void)
{
    dma_copy_banks(l2_back, ROOM_BANK, 2);  /* (96 rows: the first 24K) */
    logic_map();                             /* (the DMA gave slots 0-1 back to the ROM) */
}

/* an ellipse's rows ORed with 0x20 in the back buffer, rows >= clip left out */
static void ellipse(int16_t cx, int16_t cy, uint8_t rx, uint8_t ry, uint8_t clip)
{
    int16_t y, y0 = cy - ry, y1 = cy + ry, lo, hi;
    uint16_t r2 = (uint16_t)ry * 2, rr = r2 * r2;
    if (!ry) return;
    tv_bank = l2_back;
    if (y0 < 0) y0 = 0;
    if (y1 >= clip) y1 = clip - 1;
    for (y = y0; y <= y1; y++) {
        int16_t d = 2 * (y - cy) + 1;
        uint16_t dd = (uint16_t)(d * d), w;
        if (dd >= rr) continue;
        w = (uint16_t)rx * isqrt16(rr - dd) / r2;
        lo = cx - (int16_t)w;
        hi = cx + (int16_t)w;
        if (lo < 0) lo = 0;
        if (hi > 255) hi = 255;
        if (lo > hi) continue;
        tv_or_span((uint8_t)y, (uint8_t)lo, (uint8_t)(hi - lo + 1), 0x20);
    }
}

void room_shadow(int16_t wx, int16_t wy, int16_t fx, int16_t fy, uint8_t rx, uint8_t ry)
{
    uint8_t kind = rooms[room_cur].shadow;
    switch (kind) {
    case 0: ellipse(wx, wy, SH_WALL_R, SH_WALL_R / 2, SH_WALL_CLIP); break;   /* (half height: the rows are doubled) */
    case 1: ellipse(fx, fy, rx, ry, 96); break;
    }
}
