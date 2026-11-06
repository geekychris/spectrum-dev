/*
 * Frank the Frog - the player, from amiga_mcp/examples/frank_the_frog/frank.c
 * (drawing moved to render.c; port: x kept in 1/16 pixel for log rides).
 */
#include "game.h"
#include "frank.h"

static int frog_col;    /* grid column (0-15) */
static int frog_row;    /* grid row (0-12) */
static int frog_x16;    /* port: x in 1/16 pixel */
static int death_timer; /* death animation countdown */
static int dying;
static int highest_row; /* highest row reached this life (for scoring) */

void frank_init(void)
{
    frog_col = GRID_COLS / 2;
    frog_row = ROW_START;
    frog_x16 = X16(frog_col * TILE_W);
    death_timer = 0;
    dying = 0;
    highest_row = ROW_START;
}

int frank_highest_row(void) { return highest_row; }

void frank_move(int dx, int dy)
{
    int new_col = frog_col + dx;
    int new_row = frog_row + dy;

    if (dying) return;

    /* Boundary check */
    if (new_col < 0 || new_col >= GRID_COLS) return;
    if (new_row < 0 || new_row >= GRID_ROWS) return;

    frog_col = new_col;
    frog_row = new_row;
    frog_x16 = X16(frog_col * TILE_W);

    /* Track highest row for forward scoring */
    if (frog_row < highest_row) {
        highest_row = frog_row;
    }
}

int frank_col(void) { return frog_col; }
int frank_row(void) { return frog_row; }
int frank_x(void) { return frog_x16 >> 4; }
int frank_x16(void) { return frog_x16; }

void frank_set_x16(int x16)
{
    frog_x16 = x16;
    frog_col = (x16 >> 4) / TILE_W;
}

void frank_start_death(void)
{
    dying = 1;
    death_timer = 30;
}

int frank_die_tick(void)
{
    if (!dying) return 0;
    death_timer--;
    if (death_timer <= 0) {
        dying = 0;
        return 0;
    }
    return 1;
}

int frank_is_dying(void) { return dying; }
int frank_death_timer(void) { return death_timer; }
