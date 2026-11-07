/* the original's game state and calls, prefixed (see orig_names.h); the
   struct as the original's st.h has it */
#include <stdint.h>
typedef struct { int type, a, b; } OStEvent;
typedef struct { int type, rot, col, row; } OPiece;
typedef struct {
    int mode;
    uint32_t seed, rng;
    uint8_t board[16 * 24];
    int queue[24], nqueue;
    int hold;
    int can_hold;
    OPiece piece;
    int level, start_level, lines, combo, pieces;
    int32_t score, time_ms;
    int fall_ms, lock_ms, resets, soft;
    int over, won;
    int rise_ms, garbage_left, last_col;
    OStEvent ev[16];
    int nev;
} OStGame;
void ost_new_game(OStGame *g, int mode, uint32_t seed, int level, int col);
int  ost_move(OStGame *g, int dir);
int  ost_rotate(OStGame *g, int dir);
int  ost_hard_drop(OStGame *g);
void ost_soft_drop(OStGame *g, int on);
int  ost_hold(OStGame *g);
void ost_add_garbage(OStGame *g, int gaps);
void ost_tick(OStGame *g, int dt_ms);
int  ost_pop_event(OStGame *g, OStEvent *e);
