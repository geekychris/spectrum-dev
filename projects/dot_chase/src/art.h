/* art.h - Dot Chase's 1-bit art for the 48K (art.c) */
#ifndef ART_H
#define ART_H

#include <stdint.h>

/* a spr8 box image, 6x6: 6 | 0x80, then 6 data bytes (see shadow48.h) */
#define IMG6 (1 + 6)
typedef uint8_t img6[IMG6];

extern img6 img_bug[4][2];          /* the ladybug: [dir][leg step] */
extern img6 img_spider[4][2];       /* [dir][leg frame] */
extern img6 img_husk[4];            /* an eaten spider going home: [dir] */
extern img6 img_bonus[7];
extern img6 img_shrink[4];          /* the end of the dying animation */
extern img6 img_seed, img_flower[2], img_gate, img_empty;
extern uint8_t img_popup[2][1 + 2 * 5]; /* a score popup: two pairs of 3x5 digits */

/* the same as pointers, [dir * 2 + frame] (no 2D indexing in hot code) */
extern const uint8_t *bug_img[8], *spider_img[8], *husk_img[4];

extern void art_init(void);
extern void art_popup(uint16_t score);  /* the original's 4-wide number, right aligned */

#endif
