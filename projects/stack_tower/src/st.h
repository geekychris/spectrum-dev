/*
 * st.h - Stack Tower's game logic: the original's st.h / st_core.c (the
 * browser game's stack-tower-core.js in C), for zsdcc. Falling pieces land
 * on the outside of a round tower of 16 columns that wrap round, so moving
 * a piece left or right is turning the tower under it. A full ring clears.
 *
 * The same random generator, bag and rules, so a seed deals the same
 * pieces as the browser game and the Amiga; times are whole milliseconds.
 * port: types narrowed for the Z80 (int is 16 bits here), the 64-bit
 * multiply in st_rand_below done in 16-bit halves, the seed's text key
 * made without sprintf; the results are the same.
 */
#ifndef ST_H
#define ST_H

#include <stdint.h>

#define ST_COLS     16
#define ST_ROWS     20
#define ST_HIDDEN   4
#define ST_TOTAL    (ST_ROWS + ST_HIDDEN)
#define ST_GARBAGE  9
#define ST_NPIECES  8
#define ST_LOCK_MS  500
#define ST_LOCK_RESETS 15
#define ST_DIG_ROWS 10
#define ST_QUEUE    24

enum { MODE_MARATHON, MODE_RISING, MODE_DIG, MODE_COUNT };

/* events, for sound and the log */
enum { STE_NONE, STE_MOVE, STE_ROTATE, STE_KICK, STE_DROP, STE_HOLD, STE_LOCK, STE_CLEAR,
       STE_LEVEL, STE_GARBAGE, STE_WIN, STE_OVER, STE_COUNT };
typedef struct { uint8_t type; int16_t a, b; } StEvent;
#define ST_MAX_EVENTS 16

typedef struct { int8_t type, rot, col, row; } Piece;

typedef struct {
    uint8_t board[ST_COLS * ST_TOTAL];   /* first: the test reads it at &game */
    Piece piece;
    uint8_t mode;
    uint32_t seed, rng;
    int8_t queue[ST_QUEUE];
    uint8_t nqueue;
    int8_t hold;                         /* -1: none */
    uint8_t can_hold;
    int16_t level, start_level, lines, combo, pieces;
    int32_t score, time_ms;
    int16_t fall_ms, lock_ms, resets;
    uint8_t soft, over, won;
    int16_t rise_ms, garbage_left;
    int8_t last_col;
    uint16_t board_ver;                  /* port: counts changes to board (the panel's map) */
    StEvent ev[ST_MAX_EVENTS];
    uint8_t nev;
} StGame;

extern const int8_t st_cells[ST_NPIECES][5][2];
extern const uint8_t st_ncells[ST_NPIECES];
extern const char *const st_piece_names[ST_NPIECES];
extern const char *const st_mode_names[MODE_COUNT];
extern const char *const st_event_names[STE_COUNT];

uint32_t st_hash(const char *s);
uint32_t st_rand32(uint32_t *a);
uint8_t st_rand_below(uint32_t *a, uint8_t n);
uint8_t st_cells_of(const Piece *p, int8_t out[5][2]);   /* columns wrapped; returns the count */
uint8_t st_collides(const StGame *g, const Piece *p);
int16_t st_interval_ms(int16_t level);
int16_t st_rise_ms(int16_t level);

void st_new_game(StGame *g, uint8_t mode, uint32_t seed, int16_t level, int8_t col);
uint8_t st_preview(StGame *g, uint8_t n, int8_t *out);
uint8_t st_move(StGame *g, int8_t dir);
uint8_t st_rotate(StGame *g, int8_t dir);
int8_t st_ghost_row(const StGame *g);
int8_t st_hard_drop(StGame *g);
void st_soft_drop(StGame *g, uint8_t on);
uint8_t st_hold(StGame *g);
void st_add_garbage(StGame *g, uint8_t gaps);
void st_tick(StGame *g, int16_t dt_ms);
uint8_t st_count_garbage_rows(const StGame *g);
uint8_t st_pop_event(StGame *g, StEvent *e);

#endif
