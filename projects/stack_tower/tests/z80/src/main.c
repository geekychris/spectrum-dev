/*
 * The rules as the Next build compiles them (zsdcc), playing the original's
 * four browser games (vectors.h, from its tests/gen_vectors.js and the
 * browser's stack-tower-core.js): moves, turns, holds and hard drops by the
 * original's script (test_cases.c play_script). Each must end with the
 * browser's pieces, score, board, queue and held piece.
 */
#include <stdint.h>
#include "zxlog.h"
#include "st.h"
#include "vectors.h"

static StGame g;

static uint32_t board_hash(void)
{
    uint32_t h = 2166136261UL;
    uint16_t i;
    for (i = 0; i < ST_COLS * ST_TOTAL; i++) { h ^= g.board[i]; h *= 16777619UL; }
    return h;
}

/* the original's play_script */
static int16_t play_script(uint8_t mode, uint32_t seed)
{
    int16_t k, i;
    st_new_game(&g, mode, seed, 1, 0);
    for (k = 0; k < 120 && !g.over && !g.won; k++) {
        int16_t r = (int16_t)(((uint32_t)k * 7 + seed) % 5), m = (int16_t)(((uint32_t)k * 13 + seed) % 9) - 4;
        if (k % 11 == 5) st_hold(&g);
        for (i = 0; i < r; i++) st_rotate(&g, i % 2 ? -1 : 1);
        for (i = 0; i < (m < 0 ? -m : m); i++) st_move(&g, m < 0 ? -1 : 1);
        st_hard_drop(&g);
    }
    return k;
}

int main(void)
{
    uint8_t v, i, ok, pass = 0;
    int8_t q[5];
    for (v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
        int16_t pieces = play_script(vectors[v].mode, vectors[v].seed);
        st_preview(&g, 5, q);
        for (ok = 1, i = 0; i < 5; i++) if (q[i] != vectors[v].queue[i]) ok = 0;
        ok = ok && pieces == vectors[v].pieces && g.score == vectors[v].score && g.lines == vectors[v].lines &&
             g.over == vectors[v].over && g.won == vectors[v].won && board_hash() == vectors[v].board &&
             g.hold == vectors[v].hold;
        zx_log("rules: game %u seed %lu pieces %d score %ld %s\n", v, vectors[v].seed, pieces, (long)g.score,
               ok ? "same" : "DIFFERENT");
        pass += ok;
    }
    zx_log("rules: done %u of %u\n", pass, (uint8_t)(sizeof(vectors) / sizeof(vectors[0])));
    for (;;) ;
}
