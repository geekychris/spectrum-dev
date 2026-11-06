/*
 * Frank the Frog for the ZX Spectrum 48K - port of amiga_mcp/examples/frank_the_frog.
 *
 * lanes.c, frank.c and score.c are the original logic (port changes marked).
 * The game loop below follows the original main.c state machine; the rest is
 * the Spectrum front end: two character rows per lane so every lane has its
 * own attribute colours, solid pre-shifted objects that rewrite themselves as
 * they move (half the lanes redrawn each frame), an XOR frog, beeper effects
 * (AY music + effects on a 128K), title screen and high scores.
 *
 * Controls: Q/A/O/P, Sinclair, Kempston; one hop per press. Fire starts.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "game.h"
#include "lanes.h"
#include "frank.h"
#include "score.h"
#include "zxgame.h"
#include "zxlog.h"
#include "zxtext.h"

/* ------------------------------------------------------------ art (1-bit, '#' = ink) */

#define OBJ_LINES 14                 /* objects use lines 1..14 of a 16-line lane */

static const char *const art_car[OBJ_LINES] = {
    "....########....", "...##......##...", "..############..", ".##############.",
    "################", "#####.####.#####", "################", "################",
    "#####.####.#####", "################", ".##############.", "..###......###..",
    "..###......###..", "................",
};
static const char *const art_bus[OBJ_LINES] = {
    "................................", ".##############################.",
    "################################", "##..##..##..##..##..##..##..##.#",
    "##..##..##..##..##..##..##..##.#", "################################",
    "################################", "################################",
    "................................", "################################",
    "################################", ".###..............######.....##.",
    "..##...............##.......##..", "................................",
};
static const char *const art_log[OBJ_LINES] = {
    "................................................",
    "..###########################################...",
    ".##############################################.",
    "###...######.#######################.##########.",
    "##..#.#######.#################.##############.#",
    "##.##.##########.#######.######################.",
    "##.##.###########################.###########.##",
    "##..#.########.###################.######.#####.",
    "###...#######################.#################.",
    ".##############.###############################.",
    "..###########################################...",
    "................................................",
    "................................................",
    "................................................",
};
static const char *const art_pads[OBJ_LINES] = {
    "....###.####........###.####....", "..#####.######....#####.######..",
    ".######.#######..######.#######.", "#######.#######.#######.#######.",
    "########..#####.########..#####.", "#######.##.####.#######.##.####.",
    "#######.##.####.#######.##.####.", "########..#####.########..#####.",
    "###############################.", ".##############..##############.",
    "..############....############..", "....########........########....",
    "................................", "................................",
};
static const char *const art_frog[16] = {
    "...##......##...", "..#..#....#..#..", "..#..######..#..", "...##########...",
    "##.##########.##", "##..########..##", ".#############.", "...##########...",
    "...##########...", "..############..", ".##.########.##.", "##..########..##",
    "##...######...##", ".#....####....#.", "................", "................",
};
static const char *const art_splat[16] = {
    "#..............#", ".#............#.", "..#....##....#..", "...#..####..#...",
    "....#.####.#....", ".....######.....", ".#############..", "..############..",
    "..############..", "..#############.", ".....######.....", "....#.####.#....",
    "...#..####..#...", "..#....##....#..", ".#............#.", "#..............#",
};
static const char *const art_home[8] = {
    "..##....", ".#..#...", ".####...", "######..", ".####...", "#.##.#..", "........", "........",
};

/* pre-shifted: 8 shifts of (bytes+1) per line */
static uint8_t pre_car[8 * 3 * OBJ_LINES];
static uint8_t pre_bus[8 * 5 * OBJ_LINES];
static uint8_t pre_log40[8 * 6 * OBJ_LINES];
static uint8_t pre_log48[8 * 7 * OBJ_LINES];
static uint8_t pre_pads[8 * 5 * OBJ_LINES];
static uint8_t pre_frog[8 * 3 * 16];
static uint8_t pre_splat[8 * 3 * 16];
static uint8_t home_tile[8];

static void art_to_bits(const char *const *art, uint8_t w, uint8_t h, uint8_t *out)
{
    uint8_t bw = (w + 7) / 8, y, x;
    memset(out, 0, bw * h);
    for (y = 0; y < h; y++) {
        const char *s = art[y];
        uint8_t *o = out + y * bw, m = 0x80;
        for (x = 0; x < w; x++) {
            if (s[x] == '#') *o |= m;
            m >>= 1;
            if (!m) { m = 0x80; o++; }
        }
    }
}

static uint8_t bits[6 * 16];

static void prep(const char *const *art, uint8_t w, uint8_t h, uint8_t *pre)
{
    art_to_bits(art, w, h, bits);
    spr_preshift(bits, (w + 7) / 8, h, pre);
}

static void art_init(void)
{
    /* the log art is 48 wide; the 40-wide log uses its first 40 columns */
    static const char *log40[OBJ_LINES];
    static char log40_rows[OBJ_LINES][41];
    uint8_t i;
    for (i = 0; i < OBJ_LINES; i++) {
        memcpy(log40_rows[i], art_log[i], 40);
        log40_rows[i][37] = log40_rows[i][38] = log40_rows[i][39] = '.';
        log40_rows[i][36] = art_log[i][46] == '#' ? '#' : '.';
        log40_rows[i][40] = 0;
        log40[i] = log40_rows[i];
    }
    prep(art_car, 16, OBJ_LINES, pre_car);
    prep(art_bus, 32, OBJ_LINES, pre_bus);
    prep(log40, 40, OBJ_LINES, pre_log40);
    prep(art_log, 48, OBJ_LINES, pre_log48);
    prep(art_pads, 32, OBJ_LINES, pre_pads);
    prep(art_frog, 16, 16, pre_frog);
    prep(art_splat, 16, 16, pre_splat);
    art_to_bits(art_home, 8, 8, home_tile);
}

/* ------------------------------------------------------------ colours */

#define A_GRASS   (PAPER_GREEN | INK_BLACK)
#define A_POND    (PAPER_BLUE | INK_GREEN | BRIGHT)
#define A_HUD     (PAPER_GREEN | INK_BLACK)

static uint8_t lane_attr(const Lane *l)
{
    switch (l->kind) {
    case KIND_CAR_RED:    return PAPER_BLACK | INK_RED | BRIGHT;
    case KIND_CAR_YELLOW: return PAPER_BLACK | INK_YELLOW | BRIGHT;
    case KIND_CAR_BLUE:   return PAPER_BLACK | INK_CYAN | BRIGHT;
    case KIND_BUS:        return PAPER_BLACK | INK_MAGENTA | BRIGHT;
    case KIND_LOG:        return PAPER_BLUE | INK_YELLOW;
    default:              return PAPER_BLUE | INK_GREEN | BRIGHT;     /* lily pads */
    }
}

static const uint8_t *obj_image(const Lane *l, uint8_t w, uint8_t *bytes)
{
    switch (l->kind) {
    case KIND_BUS:  *bytes = 4; return pre_bus;
    case KIND_LOG:  if (w == 48) { *bytes = 6; return pre_log48; }
                    *bytes = 5; return pre_log40;
    case KIND_PADS: *bytes = 4; return pre_pads;
    default:        *bytes = 2; return pre_car;
    }
}

/* ------------------------------------------------------------ playfield */

static void draw_home_row(void)
{
    uint8_t s;
    attr_fill(0, 0, 32, 2, A_GRASS);
    gfx_clear_rect(0, 0, 32, 16);
    for (s = 0; s < 32; s += 3) gfx_fill_rect(s, 12, 1, 4, 0x44);    /* reeds */
    for (s = 0; s < NUM_HOMES; s++) {
        uint8_t c = (lanes_home_cx(s) - 16) / 8;          /* 4 cells of water */
        attr_fill(c, 0, 4, 2, A_POND);
        gfx_clear_rect(c, 0, 4, 16);
        if (home_filled[s]) {
            tile_draw(c + 1, 0, home_tile);
            tile_draw(c + 2, 1, home_tile);
        }
    }
}

static void draw_playfield(void)
{
    uint8_t i, x;
    screen_clear(A_GRASS, INK_GREEN);
    draw_home_row();
    for (i = 0; i < NUM_LANES; i++)
        attr_fill(0, row_y[lanes[i].row] / 8, 32, 2, lane_attr(&lanes[i]));
    attr_fill(0, row_y[ROW_MEDIAN] / 8, 32, 1, A_GRASS);
    attr_fill(0, row_y[ROW_START] / 8, 32, 1, A_HUD);
    /* dashed dividers on the last line of road lanes 5..2 */
    for (i = ROW_ROAD_5; i < ROW_ROAD_1; i++)
        for (x = 0; x < 32; x += 3) gfx_fill_rect(x, row_y[i] + 15, 1, 1, 0xFF);
    /* reeds on the median */
    for (x = 1; x < 32; x += 4) gfx_fill_rect(x, row_y[ROW_MEDIAN] + 4, 1, 4, 0x24);
}

/* ------------------------------------------------------------ lane objects */

/* where each object is drawn (pixel x, or -1000 = not drawn) */
static WORD drawn_x[NUM_LANES][MAX_LANE_OBJECTS];

/* lane_obj.asm */
extern uint8_t lo_y, lo_col, lo_n, lo_c1, lo_w1, lo_c2, lo_w2;
extern const uint8_t *lo_src;
extern void lane_obj(void);

static const uint8_t *shift_of(const uint8_t *pre, uint8_t n, uint8_t s)
{
    uint16_t step = (uint16_t)n * OBJ_LINES;
    while (s--) pre += step;
    return pre;
}

static void draw_lane(uint8_t i)
{
    Lane *l = &lanes[i];
    LaneObj *o = l->objs;
    WORD *dx = drawn_x[i];
    uint8_t j, y = row_y[l->row] + 1;
    for (j = 0; j < l->num_objs; j++, o++, dx++) {
        WORD x = lane_obj_x(o), old = *dx;
        uint8_t visible = x >= 0 && x + o->width <= SCREEN_W;   /* the original's rule */
        uint8_t nbytes, ob0 = 0, ob1 = 0, nb0 = 0, nb1 = 0;
        const uint8_t *img;
        if (old == x && visible) continue;
        if (old == -1000 && !visible) continue;
        img = obj_image(l, o->width, &nbytes);
        lo_y = y;
        lo_w1 = lo_w2 = 0;
        lo_n = 0;
        if (old != -1000) {
            ob0 = old >> 3;
            ob1 = ((old + o->width - 1) >> 3) + 1;
            if (ob1 > 31) ob1 = 31;
        }
        if (visible) {
            nb0 = x >> 3;
            nb1 = nb0 + nbytes;                   /* image is nbytes+1 wide */
            if (nb1 > 31) nb1 = 31;
            lo_col = nb0;
            lo_n = nb1 - nb0 + 1;
            lo_src = shift_of(img, nbytes + 1, x & 7);
            if (lo_n != nbytes + 1) { lo_n = 0; *dx = -1000; lo_c1 = ob0; lo_w1 = old != -1000 ? ob1 - ob0 + 1 : 0; lane_obj(); continue; }
            if (old != -1000) {                   /* trail on either side */
                if (ob0 < nb0) { lo_c1 = ob0; lo_w1 = nb0 - ob0; }
                if (ob1 > nb1) { lo_c2 = nb1 + 1; lo_w2 = ob1 - nb1; }
            }
            *dx = x;
        }
        else {                                    /* went off screen: wipe it */
            lo_c1 = ob0;
            lo_w1 = ob1 - ob0 + 1;
            *dx = -1000;
        }
        lane_obj();
    }
}

static void reset_lane_view(void)
{
    uint8_t i, j;
    for (i = 0; i < NUM_LANES; i++)
        for (j = 0; j < MAX_LANE_OBJECTS; j++) drawn_x[i][j] = -1000;
}

/* ------------------------------------------------------------ frog (XOR) + its colours */

static WORD frog_dx = -1, frog_dy;           /* drawn at, -1 = not drawn */
static uint8_t frog_dsplat;
static uint8_t frog_attr_save[3 * 2];

/* the frog's colour in a cell: dark on grass, bright green on road/water */
static uint8_t frog_cell(uint8_t attr)
{
    uint8_t paper = attr & 0x38;
    if (paper == PAPER_GREEN) return paper | INK_BLACK;
    return paper | INK_GREEN | BRIGHT;
}

static void frog_xor(WORD x, WORD y, uint8_t splat)
{
    spr_xor(x, y, splat ? pre_splat : pre_frog, 2, 16);
}

static void frog_erase(void)
{
    uint8_t c, n;
    uint8_t *a;
    if (frog_dx < 0) return;
    frog_xor(frog_dx, frog_dy, frog_dsplat);
    a = (uint8_t *)0x5800 + ((uint16_t)(frog_dy >> 3) << 5) + (frog_dx >> 3);
    n = (frog_dx >> 3) < 30 ? 3 : 32 - (frog_dx >> 3);
    for (c = 0; c < n; c++) { a[c] = frog_attr_save[c]; a[c + 32] = frog_attr_save[c + 3]; }
    frog_dx = -1;
}

static void frog_draw(void)
{
    uint8_t c, n, row = frank_row();
    WORD x = frank_x(), y = row_y[row] + row_h[row] - 16;
    uint8_t *a;
    if (x < 0) x = 0;
    if (x > 240) x = 240;
    a = (uint8_t *)0x5800 + ((uint16_t)(y >> 3) << 5) + (x >> 3);
    n = (x >> 3) < 30 ? 3 : 32 - (x >> 3);
    for (c = 0; c < n; c++) {
        frog_attr_save[c] = a[c];
        frog_attr_save[c + 3] = a[c + 32];
        a[c] = frog_cell(a[c]);
        a[c + 32] = frog_cell(a[c + 32]);
    }
    frog_dsplat = frank_is_dying();
    frog_xor(x, y, frog_dsplat);
    frog_dx = x;
    frog_dy = y;
}

/* ------------------------------------------------------------ HUD */

static long hud_score = -1;
static int hud_lives = -1, hud_level = -1;

static void hud(void)
{
    uint8_t i;
    if (hud_score != score_get()) {
        text_num(3, 23, score_get(), 6);
        hud_score = score_get();
    }
    if (hud_lives != score_lives() || hud_level != score_level()) {
        for (i = 0; i < 4; i++) {
            if (i < score_lives()) tile_draw(10 + i, 23, home_tile);
            else gfx_clear_rect(10 + i, 184, 1, 8);
        }
        zx_print_at(27, 23, "LV");
        text_num(29, 23, score_level(), 2);
        hud_lives = score_lives();
        hud_level = score_level();
    }
}

static void game_screen(void)
{
    frog_dx = -1;
    draw_playfield();
    reset_lane_view();
    zx_print_at(0, 23, "SC");
    hud_score = -1;
    hud_lives = hud_level = -1;
    hud();
}

/* ------------------------------------------------------------ sound */

/* AY periods (1.7734 MHz clock) for E3 F3 G3 A3 */
#define AY_E2 672
#define AY_F2 635
#define AY_G2 566
#define AY_A2 504
static const ay_note riff[] = {          /* the original's "fast E-F-E-F chromatic riff", on the AY */
    {AY_E2, 6}, {AY_E2, 3}, {AY_F2, 3}, {AY_E2, 6}, {AY_G2, 6}, {AY_E2, 6}, {AY_E2, 3}, {AY_F2, 3},
    {AY_E2, 6}, {AY_A2, 6}, {AY_E2, 6}, {AY_E2, 3}, {AY_F2, 3}, {AY_G2, 6}, {AY_F2, 6}, {0, 0},
};

enum { S_HOP, S_SPLAT, S_SPLASH, S_HOME, S_LEVEL, S_OVER };

static void sound(uint8_t s)
{
    if (ay_present) {
        switch (s) {
        case S_HOP:    ay_sfx(0, 180, 12, 3, -20); break;
        case S_SPLAT:  ay_sfx(1, 0, 15, 1, 0); break;
        case S_SPLASH: ay_sfx(1, 300, 15, 1, 25); break;
        case S_HOME:   ay_sfx(0, 120, 15, 1, -4); break;
        case S_LEVEL:  ay_sfx(0, 300, 15, 1, -10); break;
        case S_OVER:   ay_sfx(0, 200, 15, 1, 15); break;
        }
        return;
    }
    switch (s) {                         /* beeper: short, blocking */
    case S_HOP:    beep_sweep(40, 20, 10, 2); break;
    case S_SPLAT:  beep_noise(500); break;
    case S_SPLASH: beep_sweep(40, 140, 12, 2); break;
    case S_HOME:   beep(60, 20); beep(45, 25); beep(30, 35); break;
    case S_LEVEL:  beep(80, 15); beep(60, 20); beep(45, 25); beep(30, 35); beep(22, 50); break;
    case S_OVER:   beep(40, 25); beep(60, 20); beep(90, 15); beep(130, 12); break;
    }
}

/* ------------------------------------------------------------ title */

static void title_screen(void)
{
    screen_clear(INK_WHITE, INK_GREEN);
    attr_fill(0, 1, 32, 2, INK_GREEN | BRIGHT);
    text_big(1, 1, "FRANK THE FROG");
    spr_or(120, 32, pre_frog, 2, 16);
    attr_fill(15, 4, 3, 2, INK_GREEN | BRIGHT);
    attr_fill(0, 7, 32, 4, INK_WHITE | BRIGHT);
    text_center(7, "HOP FRANK ACROSS THE ROAD AND");
    text_center(8, "THE RIVER INTO THE FOUR PONDS");
    text_center(9, "10 PER NEW ROW  50 PER POND");
    text_center(10, "1000 FOR EACH LEVEL CLEARED");
    attr_fill(0, 12, 32, 1, INK_YELLOW | BRIGHT);
    text_center(12, "HIGH SCORES");
    attr_fill(0, 13, 32, 5, INK_WHITE | BRIGHT);
    hs_draw(10, 13);
    attr_fill(0, 23, 32, 1, INK_GREEN | BRIGHT);
    text_center(23, "QAOP/KEMPSTON/SINCLAIR  FIRE");
}

/* ------------------------------------------------------------ test probe */

/* for test bots (test.py reads it and `lanes` by symbol): the frog */
struct { int16_t frog_x, frog_row, state; } frank_probe;

static void update_probe(int state)
{
    frank_probe.frog_x = frank_x();
    frank_probe.frog_row = frank_row();
    frank_probe.state = state;
}

/* ------------------------------------------------------------ game loop (the original's) */

static int game_state = STATE_TITLE;
static int transition_frames;
static uint8_t half;                /* which half of the lanes is redrawn this frame */
static uint8_t screen_dirty;        /* a state change needs a full redraw */
static uint8_t home_dirty;          /* a pond was filled: redraw the home bank */

static void log_state(void)
{
    static int last_state = -1, last_lives = -1, last_level = -1, last_row = -1;
    static long last_score = -1;
    if (game_state != last_state) {
        static const char *names[5] = {"title", "playing", "dying", "level", "gameover"};
        zx_log("frank: state=%s score=%ld lives=%d level=%d\n", names[game_state], score_get(),
               score_lives(), score_level());
        last_state = game_state;
    }
    if (score_get() != last_score) { zx_log("frank: score=%ld\n", score_get()); last_score = score_get(); }
    if (score_lives() != last_lives) { zx_log("frank: lives=%d\n", score_lives()); last_lives = score_lives(); }
    if (score_level() != last_level) { zx_log("frank: level=%d\n", score_level()); last_level = score_level(); }
    if (frank_row() != last_row && game_state == STATE_PLAYING) {
        zx_log("frank: row=%d col=%d\n", frank_row(), frank_col());
        last_row = frank_row();
    }
}

static void die(const char *why, uint8_t snd)
{
    zx_log("frank: death %s row=%d col=%d\n", why, frank_row(), frank_col());
    frank_start_death();
    game_state = STATE_DYING;
    sound(snd);
}

/* one 50 Hz tick of the original game loop */
static void game_tick(int joy_dx, int joy_dy, int fire_pressed)
{
    int ride_dx16, death_done;

    switch (game_state) {
    case STATE_TITLE:
        if (fire_pressed) {
            game_state = STATE_PLAYING;
            score_init();
            frank_init();
            lanes_init(1);
            lanes_reset_home();
            screen_dirty = 1;
        }
        break;

    case STATE_PLAYING:
        /* Move frog */
        if (joy_dx || joy_dy) {
            int old_highest = frank_highest_row();
            frank_move(joy_dx, joy_dy);
            sound(S_HOP);
            /* Score for reaching a new highest row */
            if (frank_highest_row() < old_highest) score_add(10);
        }

        /* Update lanes */
        lanes_tick();

        /* River riding */
        if (frank_row() >= ROW_RIVER_5 && frank_row() <= ROW_RIVER_1) {
            if (!lanes_check_river(frank_x(), frank_row(), &ride_dx16)) {
                die("splash", S_SPLASH);
            } else if (ride_dx16 != 0) {
                int new_x16 = frank_x16() + ride_dx16;
                int new_x = new_x16 >> 4;
                if (new_x < -4 || new_x + TILE_W > SCREEN_W + 4) {
                    die("carried", S_SPLASH);
                } else {
                    frank_set_x16(new_x16);
                }
            }
        }

        /* Car collision */
        if (game_state == STATE_PLAYING &&
            frank_row() >= ROW_ROAD_5 && frank_row() <= ROW_ROAD_1) {
            if (lanes_check_car(frank_x(), frank_row())) die("splat", S_SPLAT);
        }

        /* Home base check */
        if (game_state == STATE_PLAYING && frank_row() == ROW_HOME) {
            int slot = lanes_check_home(frank_x());
            if (slot >= 0) {
                lanes_fill_home(slot);
                score_add(50);
                sound(S_HOME);
                zx_log("frank: home slot=%d\n", slot);
                home_dirty = 1;
                if (lanes_all_home()) {
                    game_state = STATE_LEVEL;
                    transition_frames = 60; /* 1.2 sec pause */
                    sound(S_LEVEL);
                    zx_log("frank: level %d complete\n", score_level());
                } else {
                    frank_init();
                }
            } else {
                die("bank", S_SPLASH);
            }
        }
        break;

    case STATE_DYING:
        death_done = !frank_die_tick();
        lanes_tick();          /* Keep lanes moving during death */
        if (death_done) {
            score_lose_life();
            if (score_lives() <= 0) {
                game_state = STATE_GAMEOVER;
                transition_frames = 30; /* brief pause before showing game over */
                sound(S_OVER);
                zx_log("frank: gameover score=%ld\n", score_get());
            } else {
                frank_init();
                game_state = STATE_PLAYING;
            }
        }
        break;

    case STATE_LEVEL:
        transition_frames--;
        if (transition_frames <= 0) {
            score_next_level();
            lanes_reset_home();
            lanes_init(score_level());
            frank_init();
            game_state = STATE_PLAYING;
            screen_dirty = 1;
        }
        break;

    case STATE_GAMEOVER:
        lanes_tick();          /* Keep lanes animating behind game over */
        if (transition_frames > 0) {
            transition_frames--;
        } else if (fire_pressed) {
            game_state = STATE_TITLE;
        }
        break;
    }
}

/* every 50 Hz interrupt: latch the keys (a tap shorter than a game frame counts) */
static void isr_work(void)
{
    input_sample();
}

int main(void)
{
    int prev_state;
    gfx_init();
    art_init();
    input_init();
    ay_init();
    hs_init(200);                      /* table 1000 800 600 400 200 */
    frame_init();
    input_latch(1);
    frame_hook = isr_work;
    lanes_init(1);
    title_screen();
    zx_log("frank: ready kempston=%u ay=%u\n", input_kempston, ay_present);

    for (;;) {
        uint8_t ticks, t, p, i;
        int dx = 0, dy = 0;
        prev_state = game_state;

        ticks = frame_wait();                   /* fixed timestep: see nova_defense */
        if (ticks > 3) ticks = 3;

        if (game_state != STATE_TITLE && !screen_dirty) {
            frog_erase();
            if (home_dirty) { draw_home_row(); home_dirty = 0; }
            if (game_state != STATE_GAMEOVER) {    /* the GAME OVER text sits on the road */
                for (i = half; i < NUM_LANES; i += 2) draw_lane(i);
                half ^= 1;
            }
            if (game_state != STATE_GAMEOVER && game_state != STATE_LEVEL) frog_draw();
            hud();
        }
        else if (game_state == STATE_TITLE && (frame_count & 31) == 0) {
            attr_fill(0, 20, 32, 1, (frame_count & 32) ? (INK_WHITE | BRIGHT) : INK_BLACK);
            text_center(20, "PRESS FIRE TO START");
        }
        if (ay_present) sound_tick();

        input_read();
        p = input_pressed();
        /* the original: one hop per new press (edge-triggered) */
        if (p & IN_LEFT) dx = -1;
        if (p & IN_RIGHT) dx = 1;
        if (p & IN_UP) dy = -1;
        if (p & IN_DOWN) dy = 1;

        for (t = 0; t < ticks; t++) {
            game_tick(dx, dy, (p & IN_FIRE) != 0);
            dx = dy = 0;
            p = 0;                              /* an edge counts once */
            log_state();
            if (game_state != prev_state || screen_dirty) break;
        }
        update_probe(game_state);

        if (screen_dirty) {
            screen_dirty = 0;
            game_screen();
            if (ay_present && game_state == STATE_PLAYING && prev_state == STATE_TITLE)
                music_play(2, riff, 8);
        }
        if (game_state != prev_state) {
            if (game_state == STATE_LEVEL) {
                frog_erase();
                attr_fill(0, 12, 32, 1, INK_WHITE | PAPER_RED | BRIGHT);
                text_center(12, "LEVEL COMPLETE!");
            }
            else if (game_state == STATE_PLAYING && prev_state == STATE_LEVEL) {
                /* game_screen() already redrawn */
            }
            else if (game_state == STATE_GAMEOVER) {
                frog_erase();
                music_stop();
                attr_fill(0, 15, 32, 2, INK_WHITE | PAPER_RED | BRIGHT);
                text_big(7, 15, "GAME OVER");
            }
            else if (game_state == STATE_TITLE) {
                int8_t place = hs_rank(score_get());
                if (place >= 0) hs_enter(place, score_get(), "frank");
                title_screen();
                zx_log("frank: title\n");
            }
        }
    }
}
