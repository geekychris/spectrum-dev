/* The copper (see nextcopper.h). */
#include <stdint.h>
#include <arch/zxn.h>
#include "nextcopper.h"
#include "nextgfx.h"
#include <z80.h>

uint8_t copper_buf[COPPER_MAX * 2 + 2];
uint16_t copper_n;

void copper_stop(void)
{
    ZXN_WRITE_REG(0x62, 0x00);           /* stop, index 0 */
    ZXN_WRITE_REG(0x61, 0x00);
}

void copper_begin(void)
{
    copper_stop();
    copper_n = 0;
}

static void put(uint8_t hi, uint8_t lo)
{
    uint8_t *p;
    if (copper_n >= COPPER_MAX) return;
    p = copper_buf + (copper_n << 1);
    p[0] = hi;
    p[1] = lo;
    copper_n++;
}

void copper_wait(uint16_t line, uint8_t h)
{
    put(0x80 | (h << 1) | ((line >> 8) & 1), (uint8_t)line);
}

void copper_move(uint8_t reg, uint8_t val)
{
    put(reg & 0x7F, val);
}

uint16_t copper_end(void)
{
    copper_stop();
    copper_buf[copper_n * 2] = 0xFF;     /* HALT: wait for a line that never comes */
    copper_buf[copper_n * 2 + 1] = 0xFF;
    z80_outp(0x243B, 0x60);              /* the list by DMA to NextReg 0x60 */
    dma_to_port(0x253B, copper_buf, copper_n * 2 + 2);
    copper_run();
    return copper_n;
}

void copper_run(void)
{
    ZXN_WRITE_REG(0x61, 0x00);
    ZXN_WRITE_REG(0x62, 0xC0);           /* %11: restart at the top of each frame */
}
