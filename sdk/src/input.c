/* Keyboard + joysticks -> IN_* bits (see zxgame.h). */
#include <stdint.h>
#include <z80.h>
#include <input.h>
#include <intrinsic.h>
#include "zxgame.h"

uint8_t input_kempston;
uint8_t input_last;
static uint8_t input_prev;

/* a key: half-row index (0 = 0xFEFE ... 7 = 0x7FFE), bit, and what it means */
typedef struct { uint8_t row, mask, bits; } keymap;   /* also input_fast.c */
const keymap input_keys[] = {
    {5, 0x02, IN_LEFT}, {5, 0x01, IN_RIGHT},          /* O P */
    {2, 0x01, IN_UP},   {1, 0x01, IN_DOWN},           /* Q A */
    {4, 0x10, IN_LEFT}, {4, 0x08, IN_RIGHT},          /* Sinclair 1: 6 7 */
    {4, 0x04, IN_DOWN}, {4, 0x02, IN_UP},             /*             8 9 */
    {4, 0x01, IN_FIRE},                               /*             0 */
    {3, 0x01, IN_LEFT}, {3, 0x02, IN_RIGHT},          /* Sinclair 2: 1 2 */
    {3, 0x04, IN_DOWN}, {3, 0x08, IN_UP},             /*             3 4 */
    {3, 0x10, IN_FIRE},                               /*             5 */
    {7, 0x01, IN_FIRE}, {7, 0x04, IN_FIRE},           /* SPACE M */
    {0, 0x02, IN_FIRE2},                              /* Z */
    {6, 0x01, IN_START},                              /* ENTER */
};
#define NKEYS (sizeof(input_keys) / sizeof(input_keys[0]))
const uint8_t input_nkeys = NKEYS;
static const uint16_t row_port[8] = {0xFEFE, 0xFDFE, 0xFBFE, 0xF7FE, 0xEFFE, 0xDFFE, 0xBFFE, 0x7FFE};
static uint8_t rows_now[8];
static uint8_t key_edges[8];            /* keys that went down since the read before (1 = yes) */

void input_init(void)
{
    uint8_t i, v = 0;
    /* a Kempston interface reads 0 in bits 5-7; no interface reads the
       floating bus (usually 0xFF) */
    for (i = 0; i < 16; i++) v |= z80_inp(0x001F);
    input_kempston = (v & 0xE0) == 0;
    input_prev = input_last = 0;
}

/* edge latching (input_latch(1)): input_sample() runs in the interrupt and
   collects keys that went down; input_pressed() then reports those */
extern uint8_t input_edge_rows[8], input_edge_kemp;
extern uint8_t input_last_rows[8], input_last_kemp;      /* input_sample's last view */
static uint8_t kemp_prev;
static uint8_t latching, latched_pressed;

void input_latch(uint8_t on)
{
    uint8_t i;
    for (i = 0; i < 8; i++) input_edge_rows[i] = 0;
    input_edge_kemp = 0;
    latching = on;
}

uint8_t (*input_decoder)(const uint8_t *rows, uint8_t pressed_is_one);   /* input_fast.c */

static uint8_t decode(const uint8_t *rows, uint8_t pressed_is_one)
{
    uint8_t i, b = 0;
    const keymap *k = input_keys;
    if (input_decoder) return input_decoder(rows, pressed_is_one);
    for (i = 0; i < NKEYS; i++, k++) {
        uint8_t hit = rows[k->row] & k->mask;
        if (pressed_is_one ? hit : !hit) b |= k->bits;
    }
    return b;
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

uint8_t input_read(void)
{
    uint8_t i, b, kn = 0;
    for (i = 0; i < 8; i++) {
        uint8_t r = z80_inp(row_port[i]);
        key_edges[i] = (uint8_t)(~r & rows_now[i] & 0x1F);   /* down now, up at the last read */
        rows_now[i] = r;
    }
    b = decode(rows_now, 0);
    /* BREAK = CAPS SHIFT + SPACE: not a fire */
    if (!(rows_now[0] & 0x01) && !(rows_now[7] & 0x01)) b = (b & ~IN_FIRE) | IN_QUIT;
    if (input_kempston) {
        kn = z80_inp(0x001F) & 0x1F;
        b |= kemp_bits(kn);
    }
    if (latching) {
        uint8_t edges[8], ke;
        intrinsic_di();
        for (i = 0; i < 8; i++) {
            edges[i] = input_edge_rows[i];
            /* a press this read found itself (before the interrupt sampled it)
               must not come back as the interrupt's edge at the next read:
               typed "BOT" came out "BBO" */
            input_last_rows[i] = input_last_rows[i] & (uint8_t)~key_edges[i];
            key_edges[i] |= edges[i];
            input_edge_rows[i] = 0;
        }
        ke = input_edge_kemp;
        input_edge_kemp = 0;
        input_last_kemp |= kn & ~kemp_prev;
        intrinsic_ei();
        /* a press = a key that went down since the last read, even if it is
           down again now after a release the read didn't see */
        latched_pressed = decode(edges, 1) | kemp_bits(ke) | (b & ~input_last);
    }
    kemp_prev = kn;
    input_prev = input_last;
    input_last = b;
    return b;
}

uint8_t input_pressed(void)
{
    return latching ? latched_pressed : (input_last & ~input_prev);
}

uint8_t key_down(uint16_t scancode)
{
    return in_key_pressed(scancode) ? 1 : 0;
}

/* letter rows: which letter each bit of each half-row is */
static const char letter_rows[8][5] = {
    {0, 'Z', 'X', 'C', 'V'}, {'A', 'S', 'D', 'F', 'G'}, {'Q', 'W', 'E', 'R', 'T'},
    {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {'P', 'O', 'I', 'U', 'Y'},
    {0, 'L', 'K', 'J', 'H'}, {0, 0, 'M', 'N', 'B'},
};

char input_letter_pressed(void)
{
    uint8_t r, b, v;
    for (r = 0; r < 8; r++) {
        v = key_edges[r];
        for (b = 0; b < 5; b++)
            if ((v & (1 << b)) && letter_rows[r][b]) return letter_rows[r][b];
    }
    return 0;
}

char input_letter(void)
{
    uint8_t r, b, v;
    for (r = 0; r < 8; r++) {
        v = z80_inp(((uint16_t)(0xFF ^ (1 << r)) << 8) | 0xFE);
        for (b = 0; b < 5; b++)
            if (!(v & (1 << b)) && letter_rows[r][b]) return letter_rows[r][b];
    }
    return 0;
}
