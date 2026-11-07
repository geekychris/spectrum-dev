/* Input for two players (see input2.h). */
#include <stdint.h>
#include <z80.h>
#include "input2.h"

uint8_t in2_rows[8];
uint8_t in2_kemp[2];
uint8_t in2_has_kemp[2];

/* a key: source, half-row, bit, what it gives */
typedef struct { uint16_t src; uint8_t row, mask, bits; } in2_keydef;
static const in2_keydef keydefs[] = {
    {IN2_QAOP, 2, 0x01, IN_UP}, {IN2_QAOP, 1, 0x01, IN_DOWN},
    {IN2_QAOP, 5, 0x02, IN_LEFT}, {IN2_QAOP, 5, 0x01, IN_RIGHT},
    {IN2_WASD, 2, 0x02, IN_UP}, {IN2_WASD, 1, 0x02, IN_DOWN},
    {IN2_WASD, 1, 0x01, IN_LEFT}, {IN2_WASD, 1, 0x04, IN_RIGHT},
    {IN2_IJKL, 5, 0x04, IN_UP}, {IN2_IJKL, 6, 0x04, IN_DOWN},
    {IN2_IJKL, 6, 0x08, IN_LEFT}, {IN2_IJKL, 6, 0x02, IN_RIGHT},
    {IN2_CURSOR, 3, 0x10, IN_LEFT}, {IN2_CURSOR, 4, 0x10, IN_DOWN},
    {IN2_CURSOR, 4, 0x08, IN_UP}, {IN2_CURSOR, 4, 0x04, IN_RIGHT}, {IN2_CURSOR, 4, 0x01, IN_FIRE},
    {IN2_SINCLAIR1, 4, 0x10, IN_LEFT}, {IN2_SINCLAIR1, 4, 0x08, IN_RIGHT},
    {IN2_SINCLAIR1, 4, 0x04, IN_DOWN}, {IN2_SINCLAIR1, 4, 0x02, IN_UP}, {IN2_SINCLAIR1, 4, 0x01, IN_FIRE},
    {IN2_SINCLAIR2, 3, 0x01, IN_LEFT}, {IN2_SINCLAIR2, 3, 0x02, IN_RIGHT},
    {IN2_SINCLAIR2, 3, 0x04, IN_DOWN}, {IN2_SINCLAIR2, 3, 0x08, IN_UP}, {IN2_SINCLAIR2, 3, 0x10, IN_FIRE},
    {IN2_SPACE, 7, 0x01, IN_FIRE}, {IN2_ENTER, 6, 0x01, IN_START},
};
#define NDEFS (sizeof(keydefs) / sizeof(keydefs[0]))

static const uint16_t kemp_port[2] = {0x001F, 0x0037};

void in2_init(void)
{
    uint8_t i, j, v;
    /* an interface reads 0 in bits 5-7; none reads the floating bus */
    for (j = 0; j < 2; j++) {
        v = 0;
        for (i = 0; i < 16; i++) v |= z80_inp(kemp_port[j]);
        in2_has_kemp[j] = (v & 0xE0) == 0;
    }
    in2_scan();
}

void in2_scan(void)
{
    uint8_t i;
    uint16_t port = 0xFEFE;
    for (i = 0; i < 8; i++, port = (port << 1) | 0x01FE) in2_rows[i] = z80_inp(port);
    for (i = 0; i < 2; i++) in2_kemp[i] = in2_has_kemp[i] ? z80_inp(kemp_port[i]) & 0x1F : 0;
}

static uint8_t kemp_bits(uint8_t r)
{
    uint8_t b = 0;
    if (r & 0x01) b |= IN_RIGHT;
    if (r & 0x02) b |= IN_LEFT;
    if (r & 0x04) b |= IN_DOWN;
    if (r & 0x08) b |= IN_UP;
    if (r & 0x10) b |= IN_FIRE;
    return b;
}

uint8_t in2_bits(uint16_t src)
{
    uint8_t i, b = 0;
    const in2_keydef *k = keydefs;
    for (i = 0; i < NDEFS; i++, k++)
        if ((k->src & src) && !(in2_rows[k->row] & k->mask)) b |= k->bits;
    if (src & IN2_KEMPSTON1) b |= kemp_bits(in2_kemp[0]);
    if (src & IN2_KEMPSTON2) b |= kemp_bits(in2_kemp[1]);
    return b;
}

void in2_update(in2_pad *p)
{
    uint8_t now = in2_bits(p->src);
    p->pressed = now & ~p->held;
    p->held = now;
}
