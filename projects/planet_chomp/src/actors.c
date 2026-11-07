/*
 * The sprites (main memory): render.c's rd_sprites says what each actor
 * shows and where (in the render phase, with the camera); act_commit, right
 * after the frame flips, uploads the patterns that changed (by DMA, from the
 * sprite banks) and writes every sprite's attributes, so they move with the
 * picture they belong to.
 *
 *   patterns  0..35  the actors, 4 each (a size up to 16 uses one)
 *            36..43  the popups 200 400 800 1600 (two each)
 *            44..52  the radar's dots, 53 the lives icon
 *   sprites   0..35  the actors, 4 each; 36..51 the popups; 52..61 the
 *            dots; 62..69 the lives
 */
#include <stdint.h>
#include <z80.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "actors.h"
#include "spr_data.h"

Act act[ACT_N];
Dot pop[POP_N], dot[DOT_N];
uint8_t lives_shown;

#define PAT_POP 36
#define PAT_DOT 44
#define PAT_LIFE 53
#define SPR_POP 36
#define SPR_DOT 52
#define SPR_LIFE 62

static uint8_t up_tex[ACT_N], up_size[ACT_N];   /* what each actor's patterns hold */

void act_init(void)
{
    uint8_t i;
    spr_init();
    pal_load(2, spr_pal, 256);
    {
        uint8_t m2 = ZXN_READ_REG(0x52);
        ZXN_WRITE_MMU2(SPR_EXTRA_PAGE);
        z80_outp(0x303B, PAT_POP);
        dma_to_port(0x5B, (const void *)0x4000, SPR_EXTRA_N * 256);
        ZXN_WRITE_MMU2(m2);
    }
    for (i = 0; i < ACT_N; i++) up_tex[i] = 0xFF;
}

/* actor i's patterns: texture t at size s from the sprite banks (two pages
   in slots 2 and 3, so a 1K quad is contiguous whatever its offset) */
static void upload(uint8_t i, uint8_t t, uint8_t s)
{
    uint32_t off = (uint32_t)t * SPR_REC;
    uint16_t len = s < SPR_SMALL ? 256 : 1024;
    uint8_t m2 = ZXN_READ_REG(0x52), m3 = ZXN_READ_REG(0x53), page;
    off += s < SPR_SMALL ? (uint16_t)s * 256 : SPR_SMALL * 256 + (uint16_t)(s - SPR_SMALL) * 1024;
    page = SPR_PAGE + (uint8_t)(off >> 13);
    ZXN_WRITE_MMU2(page);
    ZXN_WRITE_MMU3(page + 1);
    z80_outp(0x303B, i * 4);
    dma_to_port(0x5B, (const void *)(0x4000 + ((uint16_t)off & 0x1FFF)), len);
    ZXN_WRITE_MMU2(m2);
    ZXN_WRITE_MMU3(m3);
}

void act_commit(void)
{
    uint8_t i, n;
    for (i = 0; i < ACT_N; i++) {
        Act *a = &act[i];
        n = i * 4;
        if (!a->show) {
            spr_hide(n); spr_hide(n + 1); spr_hide(n + 2); spr_hide(n + 3);
            continue;
        }
        if (a->tex != up_tex[i] || a->size != up_size[i]) {
            upload(i, a->tex, a->size);
            up_tex[i] = a->tex;
            up_size[i] = a->size;
        }
        if (a->size < SPR_SMALL) {
            spr_show(n, a->x, a->y, n, 0);
            spr_hide(n + 1); spr_hide(n + 2); spr_hide(n + 3);
        } else
            spr_show_quad(n, a->x, a->y, n, 0);
    }
    for (i = 0; i < POP_N; i++) {
        n = SPR_POP + i * 2;
        if (!pop[i].pat) { spr_hide(n); spr_hide(n + 1); continue; }
        spr_show(n, pop[i].x, pop[i].y, PAT_POP + (pop[i].pat - 1) * 2, 0);
        spr_show(n + 1, pop[i].x + 16, pop[i].y, PAT_POP + (pop[i].pat - 1) * 2 + 1, 0);
    }
    for (i = 0; i < DOT_N; i++) {
        if (!dot[i].pat) { spr_hide(SPR_DOT + i); continue; }
        spr_show(SPR_DOT + i, dot[i].x - 7, dot[i].y - 7, PAT_DOT + dot[i].pat - 1, 0);
    }
    for (i = 0; i < 8; i++) {
        if (i < lives_shown) spr_show(SPR_LIFE + i, 14 + i * 14 - 7, 244 - 7, PAT_LIFE, 0);
        else spr_hide(SPR_LIFE + i);
    }
}
