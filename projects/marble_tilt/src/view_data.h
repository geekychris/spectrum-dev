/* made by tools/gen_view.py: the board's tables (see there) */
#ifndef VIEW_DATA_H
#define VIEW_DATA_H
#include <stdint.h>
#define VIEW_PAGE   52         /* vertex tables: 4 tilts a page, 2048 bytes each */
#define VIEW_TILTS  5           /* steps each way */
#define VIEW_LIST_PAGE 91     /* the polygon lists, read at 0x4000 */
#define VIEW_LAST_BANK 45
#define SPR_BANK    46          /* sprite patterns: 0 marble, 1-9 gauge, 10 its dot, 11.. glyphs */
#define SPR_GLYPHS  "MARBLETIHOUGNP0123456789!:"
#define SPR_INK     138         /* sprite palette: glyph ink (+16 a palette offset), the gauge */
#define SPR_RING    139
#define SPR_CROSS   140
#define SPR_DOT     141
extern const uint16_t view_lists[8];       /* each level's list in VIEW_LIST_PAGE (address in slot 2) */
extern const uint8_t view_bump_page[8];    /* 0: no bumpers; 256 bytes a tilt, 32 a page */
extern const uint16_t view_palette[128];   /* RGB333, Layer 2 0..127 */
#endif
