/*
 * Just enough of AmigaOS graphics.library and the original's game.h for
 * tools/amiga_draw.c on the host: a RastPort that draws pens into fb[]
 * (one byte a pixel, 255 = nothing drawn).
 */
#ifndef AMIGA_SHIM_H
#define AMIGA_SHIM_H

typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef short WORD;
typedef unsigned short UWORD;
typedef long LONG;

#define SCREEN_W 320
#define HUD_Y 224
#define TILE_SIZE 16
#define PLAYER_W 14
#define CHAR_RJ 0
#define CHAR_DALE 1
#define DIR_RIGHT 1
#define DIR_LEFT (-1)
#define MAX_ENEMIES 16
#define MAX_ITEMS 24

enum { COL_SKY, COL_SKY2, COL_BROWN, COL_DKBROWN, COL_GREEN, COL_DKGREEN, COL_ORANGE,
       COL_DKORANGE, COL_SKIN, COL_GREY, COL_BLUE, COL_WHITE, COL_BLACK, COL_YELLOW,
       COL_RED, COL_LTGREEN };
enum { TILE_EMPTY, TILE_GROUND, TILE_GRASS, TILE_BRICK, TILE_QBLOCK, TILE_STONE, TILE_PIPE_TL,
       TILE_PIPE_TR, TILE_PIPE_BL, TILE_PIPE_BR, TILE_PLATFORM, TILE_CLOUD_L, TILE_CLOUD_R,
       TILE_QBLOCK_HIT, TILE_FLAG, TILE_BUSH };
#define ENT_BEETLE 2
#define ENT_FLY 3
#define ENT_SPIDER 4
#define ENT_ANT 5
#define ENT_BOARD 10
#define ENT_DISKETTE 11
#define ENT_BALL 12
#define ENT_PILLOW_C 13
#define ENT_PLANT 14
#define ENT_BRIEFCASE 20
#define ENT_HEART 21
#define ENT_PILLOW_P 22
#define ENT_BEER 23
#define ENT_WINE 24

struct RastPort { int pen, cx, cy; };

#define FB_W 64
#define FB_H 64
extern unsigned char fb[FB_H][FB_W];

void SetAPen(struct RastPort *rp, long pen);
void RectFill(struct RastPort *rp, long x0, long y0, long x1, long y1);
void Move(struct RastPort *rp, long x, long y);
void Draw(struct RastPort *rp, long x, long y);

typedef struct { WORD x, y, vx, vy; BYTE dir, on_ground, character, health, max_health, lives;
                 LONG score; BYTE state, frame, frame_timer; UBYTE invuln, speed_boost; BYTE walk_frame; } Player;
typedef struct { BYTE type; WORD x, y, vx, vy; BYTE dir, alive, frame, timer; } Enemy;
typedef struct { BYTE type; WORD x, y; BYTE active; UBYTE frame; BYTE collected, float_off; } Item;
typedef struct { Enemy enemies[MAX_ENEMIES]; Item items[MAX_ITEMS]; } Game;
extern Game game;

#endif
