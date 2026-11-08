/* made by tools/gen_data.py - do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>

/* a view: the first 8K page of its stream, the screen point its rows are relative to */
typedef struct { uint8_t page; int16_t cx, cy; } View;
/* a picture: 256 x 96 texels in 3 pages; its palette (224 x 2 bytes) and tile classes
   (3 grids x 256) at meta offset meta in the meta pages */
typedef struct { const char *title; uint8_t level, room, page; uint16_t meta; } Pic;
/* a room: 256 x 192 at 4 bits a pixel in 3 pages; its 32 palette entries at meta; shadow
   0 on the back wall, 1 on the floor, 2 none */
typedef struct { const char *title; uint8_t page; uint16_t meta; uint8_t shadow; } Room;

#define FIRST_DATA_BANK 30
#define LAST_DATA_BANK 109
#define NPITCH 5
#define PITCH0 2       /* the level view */
#define PITCH_DEFAULT 3  /* the original's 12 degrees */
#define NPICS 18      /* 0: the Boing ball, made by the game */
#define NROOMS 9
#define META_PAGE 216
#define META_PAGES 3
#define NRING 118
#define BOUNCE_T 65
#define SWAY_T 571
#define BOUNCE_S 60
#define SWAY_S 393
/* render.c shadow(): on the back wall a disc up and right of the ball, above the floor line;
   on the floor an ellipse under it (port: as dark at any height) */
#define SH_WALL_R 41
#define SH_WALL_CLIP 149      /* rows below this are floor */
#define SH_PLAY_WX 158
#define SH_PLAY_WY 101
#define SH_PLAY_FX 128
#define SH_PLAY_FY 192
#define SH_PLAY_RX 64
#define SH_PLAY_RY 20
#define SH_SELECT_WX 196
#define SH_SELECT_WY 100
#define SH_SELECT_FX 190
#define SH_SELECT_FY 184
#define SH_SELECT_RX 59
#define SH_SELECT_RY 17
#define SH_TITLE_WX 158
#define SH_TITLE_WY 101
#define SH_TITLE_FX 128
#define SH_TITLE_FY 174
#define SH_TITLE_RX 53
#define SH_TITLE_RY 14
extern const int8_t pitch_deg[NPITCH];
extern const View v_play[NPITCH], v_flip[NPITCH], v_fstat[NPITCH], v_title, v_select, v_ct;
extern const Pic pics[NPICS];
extern const Room rooms[NROOMS];
extern const uint8_t ring_uv[NRING][2];   /* the flip ring: texels (u from the half's middle, v) */
extern const int8_t bounce_t[BOUNCE_T], sway_t[SWAY_T], bounce_s[BOUNCE_S], sway_s[SWAY_S];
extern const int8_t wall_dy_t[BOUNCE_T], wall_dx_t[SWAY_T], wall_dy_s[BOUNCE_S], wall_dx_s[SWAY_S];
#endif
