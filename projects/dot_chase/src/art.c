/*
 * art.c - Dot Chase's pictures, drawn for 6x6-pixel tiles on the 48K.
 *
 * port: the original draws 8x8 tiles and 7x7 sprites with RectFill in 16
 * colours; here each is 6x6 pixels of one colour (the cell's attribute),
 * designed after the original's shapes: the ladybug with its spots, head
 * and walking legs, round spiders with eight scuttling legs and eyes on the
 * side they face, the curled-up husk, the garden treats, hedges with leafy
 * speckles (drawn in main.c), seeds, pulsing flowers and the nest's
 * wooden gate. Sprites face right here and are turned for
 * the other directions at start-up, as the original's rot_rect turns them.
 */
#include <string.h>
#include "art.h"
#include "game.h"

img6 img_bug[4][2], img_spider[4][2], img_husk[4], img_bonus[7], img_shrink[4];
img6 img_seed, img_flower[2], img_gate, img_empty;
uint8_t img_popup[2][1 + 2 * 5];
const uint8_t *bug_img[8], *spider_img[8], *husk_img[4];

typedef const char *art6[6];

static const art6 a_bug[2] = {
    { ".###..", "#.##.#", "####.#", "####.#", "#.##.#", ".###.." },
    { "#.#...", ".####.", "#.##.#", "#.##.#", ".####.", "#.#..." },
};
static const art6 a_spider[2] = {
    { "#.##.#", ".####.", "####.#", "####.#", ".####.", "#.##.#" },
    { ".#..#.", "######", "####.#", "####.#", "######", ".#..#." },
};
static const art6 a_husk = { "......", "......", "..##.#", "..##..", "......", "......" };
static const art6 a_bonus[7] = {
    { "...#..", "..#...", ".##.##", ".##.##", "..###.", "..###." },    /* berry */
    { "..##..", ".####.", "#.##.#", "######", "..##..", "..##.." },    /* mushroom */
    { ".#.#..", "..#...", "#####.", ".###..", "..##..", "..#..." },    /* carrot */
    { "...#..", "..#...", "..##..", ".####.", ".####.", "..##.." },    /* pear */
    { "..#...", "######", "######", ".####.", ".####.", "..##.." },    /* acorn */
    { ".####.", "######", "#....#", "######", "######", ".####." },    /* honey pot */
    { ".#.#..", "#####.", "##.##.", "#####.", ".#.#..", "..#..." },    /* sunflower */
};
static const art6 a_shrink[4] = {
    { "######", "######", "##..##", "##..##", "######", "######" },
    { "......", ".####.", ".####.", ".####.", ".####.", "......" },
    { "......", "......", "..##..", "..##..", "......", "......" },
    { "......", "......", "..#...", "......", "......", "......" },
};
static const art6 a_seed = { "......", "......", "...#..", "..##..", "......", "......" };
static const art6 a_flower[2] = {
    { "......", "......", "..##..", "..##..", "......", "......" },
    { "..##..", ".#..#.", "#.##.#", "#.##.#", ".#..#.", "..##.." },
};
static const art6 a_empty = { "......", "......", "......", "......", "......", "......" };
static const art6 a_gate = { "#....#", "######", "#....#", "#....#", "######", "#....#" };

/* art -> spr8 box image: the whole 6x6 box is the mask (sprites are opaque) */
static void make(uint8_t *img, const char *const *a)
{
    uint8_t y, x;
    img[0] = 6 | 0x80;
    for (y = 0; y < 6; y++) {
        uint8_t d = 0;
        for (x = 0; x < 6; x++)
            if (a[y][x] == '#') d |= 0x80 >> x;
        img[1 + y] = d;
    }
}

/* the right-facing image turned to face dir (the original's rot_rect) */
static void turn(uint8_t *dst, const uint8_t *src, uint8_t dir)
{
    uint8_t y, x;
    dst[0] = 6 | 0x80;
    for (y = 0; y < 6; y++) {
        uint8_t d = 0;
        for (x = 0; x < 6; x++) {
            uint8_t sy = y, sx = x;
            switch (dir) {
            case DIR_DOWN: sy = x; sx = y; break;
            case DIR_LEFT: sx = 5 - x; break;
            case DIR_UP:   sy = x; sx = 5 - y; break;
            }
            if (src[1 + sy] & (0x80 >> sx)) d |= 0x80 >> x;
        }
        dst[1 + y] = d;
    }
}

static void make_turned(img6 out[4], const char *const *a)
{
    uint8_t d;
    make(out[DIR_RIGHT], a);
    for (d = 1; d < 4; d++) turn(out[d], out[DIR_RIGHT], d);
}

void art_init(void)
{
    uint8_t i;
    img6 t[4];

    for (i = 0; i < 2; i++) {
        uint8_t d;
        make_turned(t, a_bug[i]);
        for (d = 0; d < 4; d++) memcpy(img_bug[d][i], t[d], IMG6);
        make_turned(t, a_spider[i]);
        for (d = 0; d < 4; d++) memcpy(img_spider[d][i], t[d], IMG6);
        make(img_flower[i], a_flower[i]);
    }
    make_turned(img_husk, a_husk);
    for (i = 0; i < 8; i++) {
        bug_img[i] = img_bug[i >> 1][i & 1];
        spider_img[i] = img_spider[i >> 1][i & 1];
    }
    for (i = 0; i < 4; i++) husk_img[i] = img_husk[i];
    for (i = 0; i < 7; i++) make(img_bonus[i], a_bonus[i]);
    for (i = 0; i < 4; i++) make(img_shrink[i], a_shrink[i]);
    make(img_seed, a_seed);
    make(img_gate, a_gate);
    make(img_empty, a_empty);
}

/* ------------------------------------------------------------ score popups */

static const uint8_t digit3x5[10][5] = {      /* 3 bits each, top bits */
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

static uint8_t dig_row(char c, uint8_t y)
{
    return c >= '0' && c <= '9' ? digit3x5[c - '0'][y] : 0;
}

void art_popup(uint16_t score)
{
    char s[4];
    uint8_t i, y;
    for (i = 4; i-- > 0;) {                 /* the original's format_number(.., 4) */
        s[i] = (score || i == 3) ? '0' + score % 10 : ' ';
        score /= 10;
    }
    for (i = 0; i < 2; i++) {
        uint8_t *p = img_popup[i];
        p[0] = 5;
        for (y = 0; y < 5; y++) {
            p[1 + 2 * y] = 0xFE;
            p[2 + 2 * y] = (dig_row(s[2 * i], y) << 5) | (dig_row(s[2 * i + 1], y) << 1);
        }
    }
}
