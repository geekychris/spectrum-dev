/* The copper (see nextcopper.h). */
#include <stdint.h>
#include <arch/zxn.h>
#include "nextcopper.h"

uint8_t copper_buf[COPPER_MAX * 2];
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
    if (copper_n >= COPPER_MAX) return;
    copper_buf[copper_n * 2] = hi;
    copper_buf[copper_n * 2 + 1] = lo;
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
    uint16_t i;
    copper_stop();
    for (i = 0; i < copper_n * 2; i++) ZXN_WRITE_REG(0x60, copper_buf[i]);
    ZXN_WRITE_REG(0x60, 0xFF);           /* HALT: wait for a line that never comes */
    ZXN_WRITE_REG(0x60, 0xFF);
    ZXN_WRITE_REG(0x61, 0x00);
    ZXN_WRITE_REG(0x62, 0xC0);           /* %11: restart at the top of each frame */
    return copper_n;
}
