/*
 * game.c - Boing Twist's game shell for the Next (the original's main_os4.c):
 * the title with the Boing ball bouncing round its room, the select screen
 * (level, picture, room), play, the solved screen, and here the high scores.
 *
 * The moves go through a queue as in the original: a move animates (a
 * twist 240 ms, a flip 420 ms; faster while scrambling or solving) and is
 * applied to the puzzle (bt_core.c, the original's, unchanged) when it
 * ends. Everything runs on 50 Hz ticks, the original's milliseconds turned
 * into ticks, whatever the drawing costs (fixed timestep): a slow picture
 * shows fewer steps of a move, not a slower move.
 *
 * port: keys. The original's arrows: Q A O P, the cursor keys or a
 * joystick (up/down choose the band, left/right twist it); its A / D (choose
 * the half) are Z / X here, or left/right with fire held; a tap of SPACE or
 * fire flips. Its Q / E (turn the ball) are K / L, W / S tilt it, C faces the
 * half, U undoes, H peeks (held), ENTER pauses (its P), R gives up, N
 * scrambles anew, T changes the room, BREAK goes back (its Esc).
 * port: the original keeps your best moves and time for each picture and
 * level in a file; here they last until the Next is reset, and a solve
 * scores points for the high-score table (the level, moves under three times
 * the scramble, seconds under five minutes, less for each peek).
 */
#include <string.h>
#include <stdio.h>
#include <input.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "zxlog.h"
#include "nextgfx.h"
#include "bt.h"
#include "bt_core.h"
#include "ball.h"
#include "room.h"
#include "hud.h"
#include "sound.h"
#include "game.h"

uint8_t screen, help, title_page, paused, peeking, gave_up, best_new, busy;
uint8_t hud_dirty;
uint8_t sel_level, sel_pic, sel_row, play_level, play_pic;
uint8_t sel_band, sel_half, pitch = PITCH_DEFAULT;
uint16_t moves_shown, secs, peeks, score;
int16_t anim_move = -1;
static uint8_t anim_kind, anim_len, anim_t, auto_solve, timer_running, sec_ticks;
static uint16_t off16, target16;          /* the ball's turn: texels x 256 added to the longitude */
static uint16_t tick_count, scene_t;      /* ticks since start; on this screen */
static uint32_t seed_ticks;
static uint8_t q_kind[128];
static int16_t q_move[128];
static uint8_t qh, qt;                    /* the move queue (a ring) */
static uint8_t ball_dirty, bg_dirty, tex_marks, tex_peek, pic_wait, flip_pending;
hs_editor ed;
static int8_t ed_place;

enum { K_DO, K_UNDO, K_SOLVE, K_SCRAMBLE };
static const char *const screen_names[] = { "TITLE", "SELECT", "PLAY", "SOLVED", "ENTRY" };

/* smoothstep: t^2 (3 - 2t), 0..256 */
static uint16_t ease(uint8_t t, uint8_t len)
{
    uint16_t x = (uint16_t)t * 256u / len;          /* 0..256 */
    return (uint16_t)(((uint32_t)x * x * (768u - 2u * x)) >> 16);
}

static void set_screen(uint8_t s)
{
    screen = s;
    scene_t = 0;
    hud_dirty = 1;
    bg_dirty = 1;
    ball_dirty = 1;
    zx_log("bt: screen %s\n", screen_names[s]);
}

/* ---------------- the picture and the texture ---------------- */

/* the pictures a level offers: the checkerboard only on Easy (its tiles look
   alike, so it is a warm-up), the rest from the level they suit up */
uint8_t pic_allowed(uint8_t i, uint8_t level)
{
    if (i == 0) return level == BT_EASY;
    return pics[i].level <= level;
}

static uint8_t next_allowed(uint8_t from, int8_t d, uint8_t level)
{
    uint8_t k, i = from;
    for (k = 0; k < NPICS; k++) {
        i = (uint8_t)((i + d + NPICS) % NPICS);
        if (pic_allowed(i, level)) return i;
    }
    return from;
}

static void texture_band(uint8_t only)
{
    uint8_t marks = screen == SC_PLAY && anim_move < 0 && !busy && !peeking;
    if (tex_marks || tex_peek) only = 0xFF;  /* (the marks cross every band) */
    ball_build(&P, (marks ? BB_MARKS : 0) | (peeking && screen == SC_PLAY ? BB_PEEK : 0), sel_band, sel_half, only);
    tex_marks = marks;
    tex_peek = peeking;
    ball_dirty = 1;
}

static void texture(void) { texture_band(0xFF); }

/* a solved ball of this picture and level's grid (the title's, the select screen's) */
static void show_picture(uint8_t pic, uint8_t level)
{
    bt_init(&P, bt_levels[level].bands, bt_levels[level].segs);
    ball_picture(pic, (uint8_t)P.bands, (uint8_t)P.segs, &P);
    texture();
}

/* ---------------- moves ---------------- */

static uint8_t anim_ticks(uint8_t kind, int16_t m)
{
    uint8_t flip = BT_IS_FLIP(m) ? 1 : 0;
    switch (kind) {
    case K_SCRAMBLE: return flip ? 8 : 5;          /* 160 / 90 ms */
    case K_SOLVE:    return flip ? 13 : 8;         /* 260 / 150 */
    default:         return flip ? 21 : 12;        /* 420 / 240 */
    }
}

static uint8_t q_len(void) { return (uint8_t)(qt - qh) & 127; }

static void enqueue(uint8_t kind, int16_t m)
{
    if (q_len() >= 127) return;
    if (kind == K_DO && !bt_valid(&P, m)) return;
    q_kind[qt] = kind;
    q_move[qt] = m;
    qt = (qt + 1) & 127;
}

static void face_half(uint8_t k)
{
    target16 = (uint16_t)ball_half_u(k) << 8;
}

static void enter_solved(void)
{
    uint16_t lm = (uint16_t)bt_levels[play_level].moves * 3, bonus;
    timer_running = 0;
    best_new = 0;
    score = 0;
    if (!gave_up) {
        uint16_t *bm = &best_mv[play_pic][play_level], *bs = &best_s[play_pic][play_level];
        bonus = 500 + (P.moves < lm ? (lm - (uint16_t)P.moves) * 25 : 0) + (secs < 300 ? (300 - secs) * 2 : 0);
        score = bonus * (play_level + 1);
        score = score > peeks * 100u ? score - peeks * 100u : 0;
        if (!*bm || (uint16_t)P.moves < *bm || secs < *bs) {
            if (!*bm || (uint16_t)P.moves < *bm) *bm = (uint16_t)P.moves;
            if (!*bs || secs < *bs) *bs = secs;
            best_new = 1;
        }
    }
    snd_play(SFX_SOLVED);
    zx_log("bt: solved moves=%d secs=%u peeks=%u score=%u gave_up=%u best=%u\n", P.moves, secs, peeks, score,
           gave_up, best_new);
    set_screen(SC_SOLVED);
    texture();
}

/* the next move off the queue, if none is running */
static void anim_next(void)
{
    if (auto_solve && q_len() == 0 && anim_move < 0) {
        if (P.nhist > 0) enqueue(K_SOLVE, 0);
        else auto_solve = 0;
    }
    while (anim_move < 0 && q_len()) {
        uint8_t kind = q_kind[qh];
        int16_t m = q_move[qh];
        qh = (qh + 1) & 127;
        if (kind == K_UNDO) m = bt_last_move(&P) >= 0 ? bt_inverse(bt_last_move(&P)) : -1;
        else if (kind == K_SOLVE) m = P.nhist > 0 ? bt_inverse(P.hist[P.nhist - 1]) : -1;
        if (m < 0) continue;
        anim_move = m;
        anim_kind = kind;
        anim_t = 0;
        anim_len = anim_ticks(kind, m);
        if (kind == K_DO && !timer_running) timer_running = 1;
        if (kind != K_SCRAMBLE || BT_IS_FLIP(m)) snd_play(BT_IS_FLIP(m) ? SFX_FLIP : SFX_CLICK);
        if (tex_marks || tex_peek) texture();      /* no selection drawn while it moves */
        if (BT_IS_FLIP(m)) {
            off16 = target16 = (uint16_t)ball_half_u((uint8_t)BT_HALF(m)) << 8;   /* port: facing the half */
            ball_flip_begin((uint8_t)BT_HALF(m));
        } else
            ball_twist_begin((uint8_t)BT_BAND(m));
        ball_dirty = 1;
    }
}

static void anim_finish(void)
{
    int16_t m = anim_move;
    uint8_t kind = anim_kind;
    switch (kind) {
    case K_DO: bt_do(&P, m); break;
    case K_UNDO: bt_undo(&P); break;
    case K_SOLVE:
        P.nhist--;
        if (P.nscramble > P.nhist) P.nscramble = P.nhist;
        bt_apply(&P, m);
        break;
    case K_SCRAMBLE:
        bt_apply(&P, m);
        if (P.nhist < BT_HIST) P.hist[P.nhist++] = m;
        P.nscramble = P.nhist;
        break;
    }
    anim_move = -1;
    busy = q_len() && q_kind[qh] == K_SCRAMBLE;
    hud_dirty = 1;
    if (kind == K_DO || kind == K_UNDO) zx_log("bt: move m=%d moves=%d\n", m, P.moves);
    if (kind == K_SCRAMBLE && !busy) zx_log("bt: scrambled n=%d hash=%08lX\n", P.nscramble, bt_hash(&P));
    if (screen == SC_PLAY && !q_len() && kind != K_SCRAMBLE && bt_solved(&P)) {
        auto_solve = 0;
        if (kind == K_SOLVE) gave_up = 1;
        enter_solved();
        return;
    }
    texture_band(BT_IS_FLIP(m) ? 0xFF : (uint8_t)BT_BAND(m));
}

/* a new puzzle of the chosen level and picture (main_os4.c start_game) */
static void start_game(void)
{
    uint8_t lv = sel_level, i;
    uint32_t seed = seed_ticks * 2654435761u + 1;
    if (!pic_allowed(sel_pic, lv)) sel_pic = next_allowed(sel_pic, 1, lv);
    play_level = lv;
    play_pic = sel_pic;
    bt_init(&P, bt_levels[lv].bands, bt_levels[lv].segs);
    ball_picture(play_pic, (uint8_t)P.bands, (uint8_t)P.segs, &P);
    /* the scramble (one that leaves a checkerboard solved is retried), shown move by move;
       port: from a queue long enough for Expert's 80 (the original's 64 drop the rest) */
    seed = bt_start_level(&P, lv, seed, -1);
    zx_log("bt: play pic=%u level=%u grid=%dx%d scramble=%d seed=%lu\n", play_pic, lv, P.bands, P.segs, P.nscramble,
           seed);
    qh = qt = 0;
    for (i = 0; i < P.nscramble; i++) enqueue(K_SCRAMBLE, P.hist[i]);
    bt_reset(&P);
    anim_move = -1;
    auto_solve = 0;
    busy = 1;
    timer_running = 0;
    secs = sec_ticks = 0;
    peeks = 0;
    gave_up = 0;
    paused = peeking = 0;
    sel_band = (uint8_t)(P.bands / 2 - 1);
    sel_half = (uint8_t)((P.segs / 2 - P.segs / 4 + P.segs) % P.segs);   /* the half facing the eye at the start */
    pitch = PITCH_DEFAULT;
    off16 = target16 = 128u << 8;              /* the original's yaw -180 */
    snd_music(0);
    set_screen(SC_PLAY);
    texture();
}

static void to_title(void)
{
    snd_music(1);
    set_screen(SC_TITLE);
    title_page = 0;
    show_picture(0, BT_HARD);                  /* the Boing ball's own 8 x 16 */
}

static void to_select(void)
{
    snd_music(1);
    set_screen(SC_SELECT);
    if (!pic_allowed(sel_pic, sel_level)) sel_pic = next_allowed(sel_pic, 1, sel_level);
    if (pics[sel_pic].room) room_load(pics[sel_pic].room);
    show_picture(sel_pic, sel_level);
}

/* a picture chosen: its room comes with it, if it names one */
static void choose_picture(uint8_t i)
{
    sel_pic = i;
    pic_wait = 12;                             /* loaded once the choice stays a quarter of a second */
    if (pics[i].room && pics[i].room != room_cur) {
        room_load(pics[i].room);
        bg_dirty = 1;
    }
    hud_dirty = 1;
}

static void change_row(int8_t d)
{
    switch (sel_row) {
    case 0:
        sel_level = (uint8_t)((sel_level + d + BT_NLEVELS) % BT_NLEVELS);
        if (!pic_allowed(sel_pic, sel_level)) choose_picture(next_allowed(sel_pic, 1, sel_level));
        pic_wait = 12;
        break;
    case 1: choose_picture(next_allowed(sel_pic, d, sel_level)); break;
    case 2:
        room_load((uint8_t)((room_cur + d + NROOMS) % NROOMS));
        bg_dirty = 1;
        break;
    }
    hud_dirty = 1;
}

/* ---------------- input ---------------- */
#define K_Z 0x0001
#define K_X 0x0002
#define K_K 0x0004
#define K_L 0x0008
#define K_W 0x0010
#define K_S 0x0020
#define K_C 0x0040
#define K_U 0x0080
#define K_H 0x0100
#define K_R 0x0200
#define K_N 0x0400
#define K_T 0x0800

static uint16_t keys_now(void)
{
    uint16_t k = 0;
    if (key_down(IN_KEY_SCANCODE_z)) k |= K_Z;
    if (key_down(IN_KEY_SCANCODE_x)) k |= K_X;
    if (key_down(IN_KEY_SCANCODE_k)) k |= K_K;
    if (key_down(IN_KEY_SCANCODE_l)) k |= K_L;
    if (key_down(IN_KEY_SCANCODE_w)) k |= K_W;
    if (key_down(IN_KEY_SCANCODE_s)) k |= K_S;
    if (key_down(IN_KEY_SCANCODE_c)) k |= K_C;
    if (key_down(IN_KEY_SCANCODE_u)) k |= K_U;
    if (key_down(IN_KEY_SCANCODE_h)) k |= K_H;
    if (key_down(IN_KEY_SCANCODE_r)) k |= K_R;
    if (key_down(IN_KEY_SCANCODE_n)) k |= K_N;
    if (key_down(IN_KEY_SCANCODE_t)) k |= K_T;
    return k;
}

static volatile uint16_t keys_latched;
static uint16_t prev_keys;
static uint8_t fire_held, fire_used;

void game_sample(void)
{
    input_sample();
    keys_latched |= keys_now();
}

static void play_input(uint8_t held, uint8_t edge, uint16_t keys, uint16_t kedge)
{
    uint8_t S = (uint8_t)P.segs, B = (uint8_t)P.bands, old_band = sel_band, old_half = sel_half;
    if (edge & IN_START) {
        paused = !paused;
        hud_dirty = 1;
    }
    if (paused) return;
    if ((keys & K_H) && !peeking) { peeking = 1; peeks++; hud_dirty = 1; texture(); }
    if (!(keys & K_H) && peeking) { peeking = 0; hud_dirty = 1; texture(); }
    /* fire held: left / right choose the half; a tap flips */
    if (edge & IN_FIRE) { fire_held = 1; fire_used = 0; }
    if (fire_held && (held & IN_FIRE)) {
        if (edge & IN_LEFT) { sel_half = (sel_half + S - 1) % S; fire_used = 1; }
        if (edge & IN_RIGHT) { sel_half = (sel_half + 1) % S; fire_used = 1; }
    } else {
        if (fire_held && !fire_used && !busy) enqueue(K_DO, BT_FLIP(sel_half));
        fire_held = 0;
        if ((edge & IN_UP) && sel_band > 0) sel_band--;
        if ((edge & IN_DOWN) && sel_band < B - 1) sel_band++;
        if ((edge & IN_LEFT) && !busy) enqueue(K_DO, BT_TWIST(sel_band, 1));    /* west */
        if ((edge & IN_RIGHT) && !busy) enqueue(K_DO, BT_TWIST(sel_band, 0));   /* east */
    }
    if (kedge & K_Z) sel_half = (sel_half + S - 1) % S;
    if (kedge & K_X) sel_half = (sel_half + 1) % S;
    if (sel_half != old_half) face_half(sel_half);
    if (kedge & K_K) target16 -= 16u << 8;    /* the original's Q: yaw + 22.5 degrees */
    if (kedge & K_L) target16 += 16u << 8;
    if ((kedge & K_W) && pitch < NPITCH - 1) { pitch++; ball_dirty = 1; }   /* port: -24..24 */
    if ((kedge & K_S) && pitch > 0) { pitch--; ball_dirty = 1; }
    if (kedge & K_C) { face_half(sel_half); pitch = PITCH_DEFAULT; ball_dirty = 1; }
    if ((kedge & K_U) && !busy) enqueue(K_UNDO, 0);
    if ((kedge & K_R) && !busy) { qh = qt; auto_solve = 1; zx_log("bt: give up\n"); }
    if (kedge & K_N) { start_game(); return; }
    if (kedge & K_T) { room_load((uint8_t)((room_cur + 1) % NROOMS)); bg_dirty = 1; }
    if ((sel_band != old_band || sel_half != old_half) && anim_move < 0 && !busy) texture();
    if (sel_band != old_band || sel_half != old_half) hud_dirty = 1;
}

static void input(void)
{
    uint8_t held = input_read(), edge = input_pressed();
    uint16_t keys = keys_now(), kedge = (keys | keys_latched) & ~prev_keys;
    keys_latched = 0;
    prev_keys = keys;
    if (screen == SC_ENTRY) return;            /* the initials editor reads the keys itself */
    if (help) {
        if (edge || kedge) { help = 0; hud_dirty = 1; }
        return;
    }
    switch (screen) {
    case SC_TITLE:
        if (edge & (IN_FIRE | IN_START)) to_select();
        else if (kedge & K_H) { help = 1; hud_dirty = 1; }
        else if (kedge & K_T) { room_load((uint8_t)((room_cur + 1) % NROOMS)); bg_dirty = 1; hud_dirty = 1; }
        break;
    case SC_SELECT:
        if (edge & IN_QUIT) to_title();
        else if (edge & IN_UP) { sel_row = (sel_row + 2) % 3; hud_dirty = 1; }
        else if (edge & IN_DOWN) { sel_row = (sel_row + 1) % 3; hud_dirty = 1; }
        else if (edge & IN_LEFT) change_row(-1);
        else if (edge & IN_RIGHT) change_row(1);
        else if (edge & (IN_FIRE | IN_START)) start_game();
        else if (kedge & K_H) { help = 1; hud_dirty = 1; }
        break;
    case SC_PLAY:
        if (edge & IN_QUIT) { qh = qt; anim_move = -1; to_title(); }
        else play_input(held, edge, keys, kedge);
        break;
    case SC_SOLVED:
        if (scene_t < 50) break;               /* a second of the bounce first */
        if (edge & IN_QUIT) to_title();
        else if (edge & (IN_FIRE | IN_START)) {
            ed_place = gave_up ? -1 : hs_rank(score);
            if (ed_place >= 0 && score > 0) {
                hs_edit_begin(&ed);
                set_screen(SC_ENTRY);
            } else to_select();
        }
        break;
    }
}

/* ---------------- a tick (50 Hz) ---------------- */
static void tick(void)
{
    int16_t d;
    seed_ticks++;
    tick_count++;
    scene_t++;
    if (help || screen == SC_ENTRY) return;
    if (screen == SC_TITLE) {
        off16 -= 256;                          /* 70 degrees a second */
        if (scene_t % BOUNCE_T == 0) snd_play(SFX_THUD);
        if (scene_t % 400 == 0) { title_page ^= 1; hud_dirty = 1; }
        ball_dirty = 1;
        return;
    }
    if (screen == SC_SELECT) {
        off16 += 91;                           /* -25 degrees a second */
        ball_dirty = 1;
        if (pic_wait && --pic_wait == 0) show_picture(sel_pic, sel_level);
        return;
    }
    if (screen == SC_SOLVED) {
        off16 -= 328;                          /* 90 degrees a second */
        if (scene_t > 10 && scene_t % BOUNCE_S == 0) snd_play(SFX_THUD);
        ball_dirty = 1;
        return;
    }
    /* play */
    if (paused) return;
    anim_next();
    if (anim_move >= 0) {
        ball_dirty = 1;
        if (++anim_t >= anim_len) { anim_finish(); anim_next(); }
    }
    if (screen != SC_PLAY) return;
    if (timer_running && ++sec_ticks >= 50) {
        sec_ticks = 0;
        secs++;
        hud_dirty = 1;
    }
    d = (int16_t)(target16 - off16);           /* turning to look: a sixth of the way a tick */
    if (d) {
        int16_t st = d / 6;
        if (st == 0) st = d;
        off16 += st;
        ball_dirty = 1;
    }
}

/* ---------------- the picture ---------------- */
static uint8_t ball_off(void) { return (uint8_t)((off16 + 128) >> 8); }

static void draw_play_ball(void)
{
    if (anim_move >= 0) {
        uint16_t e = ease(anim_t, anim_len);
        if (BT_IS_FLIP(anim_move)) {
            ball_draw_flip(pitch, (uint8_t)BT_HALF(anim_move), (uint8_t)((e * 128u + 128) >> 8));
            return;
        } else {
            uint8_t w = (uint8_t)(256u / P.segs), dd = (uint8_t)((e * w + 128) >> 8);
            ball_twist_shift((uint8_t)BT_BAND(anim_move), BT_WEST(anim_move) ? (uint8_t)-dd : dd);
        }
    }
    ball_draw(&v_play[pitch], ball_off(), v_play[pitch].cx, v_play[pitch].cy);
}

/* the room and the shadow in both screens (the ball stays put on these) */
static void backgrounds(int16_t wx, int16_t wy, int16_t fx, int16_t fy, uint8_t rx, uint8_t ry)
{
    uint8_t k;
    for (k = 0; k < 2; k++) {
        room_to_back();
        room_shadow(wx, wy, fx, fy, rx, ry);
        l2_flip();
    }
}

static uint16_t drawn, pixels_last;

static void picture(void)
{
    if (help || screen == SC_ENTRY) { bg_dirty = 0; return; }
    switch (screen) {
    case SC_TITLE:
    case SC_SOLVED: {
        /* the 1984 demo: bouncing across the room, spinning on a leaning axis */
        uint16_t t = scene_t;
        uint8_t ib = screen == SC_TITLE ? t % BOUNCE_T : t % BOUNCE_S, ix = screen == SC_TITLE ? t % SWAY_T : t % SWAY_S;
        int16_t x = v_title.cx + (screen == SC_TITLE ? sway_t[ix] : sway_s[ix]);
        int16_t y = v_title.cy + (screen == SC_TITLE ? bounce_t[ib] : bounce_s[ib]);
        int16_t wx = 128 + (screen == SC_TITLE ? wall_dx_t[ix] : wall_dx_s[ix]);
        int16_t wy = 48 + (screen == SC_TITLE ? wall_dy_t[ib] : wall_dy_s[ib]);
        room_to_back();
        room_shadow(wx, wy, x, SH_TITLE_FY, SH_TITLE_RX, SH_TITLE_RY);
        ball_draw(&v_title, ball_off(), x, y);
        break;
    }
    case SC_SELECT:
        if (bg_dirty) backgrounds(SH_SELECT_WX, SH_SELECT_WY, SH_SELECT_FX, SH_SELECT_FY, SH_SELECT_RX, SH_SELECT_RY);
        if (!ball_dirty) return;
        ball_draw(&v_select, ball_off(), v_select.cx, v_select.cy);
        break;
    case SC_PLAY:
        if (bg_dirty) {
            backgrounds(SH_PLAY_WX, SH_PLAY_WY, SH_PLAY_FX, SH_PLAY_FY, SH_PLAY_RX, SH_PLAY_RY);
            ball_dirty = 2;                    /* both screens */
        }
        if (!ball_dirty) return;
        draw_play_ball();
        break;
    }
    bg_dirty = 0;
    if (ball_dirty) ball_dirty--;
    flip_pending = 1;
    drawn++;
    pixels_last = rv_pixels;
}

/* ---------------- the main loop's pass ---------------- */
static uint16_t fps_frames, fps_drawn, fps_ticks, fps_busy;
static uint8_t drew_last;

void game_after_wait(void)
{
    if (flip_pending) {
        l2_flip();
        flip_pending = 0;
    }
}

/* the original's self-test (its selftest hook): bt_vector_hash(), the long
   run its host tests fold into tests/vectors.h. Run at start-up while T is
   held (test.py) */
static void selftest(void)
{
    uint32_t h = bt_vector_hash();
    zx_log("bt: selftest hash=%08lX\n", h);
}

void game_init(void)
{
    if (key_down(IN_KEY_SCANCODE_t)) selftest();
    hs_init(400);
    sel_level = BT_EASY;
    sel_pic = 0;
    sel_row = 1;
    room_load(0);
    to_title();
}

void game_frame(uint8_t frames)
{
    uint8_t steps;
    input();
    if (screen == SC_ENTRY) {
        uint8_t r = hs_edit_step(&ed);
        if (r & HS_EDIT_TAKEN) snd_play(SFX_KEY);
        if (r & HS_EDIT_CHANGED) hud_dirty = 1;
        if (r & HS_EDIT_DONE) {
            hs_insert(ed_place, ed.name, score);
            zx_log("bt: hiscore %s %u place=%d\n", ed.name, score, ed_place + 1);
            to_title();
            title_page = 1;
        }
    }
    steps = frames > 12 ? 12 : frames;        /* catch up at most 12 ticks a pass */
    fps_ticks += steps;
    while (steps--) tick();
    picture();
    if (hud_dirty) {
        hud_dirty = 0;
        screens_draw();
    }
    fps_frames += frames;
    if (drew_last) fps_busy += frames;        /* the frames the last drawing took */
    drew_last = flip_pending;
    if (flip_pending) fps_drawn++;
    while (frames--) snd_tick();
    if (fps_frames >= 100) {
        /* fps10: pictures a second while it moves (passes that drew, over the frames they took) */
        zx_log("bt: perf fps10=%u drawn=%u busy=%u ticks=%u screen=%u px=%u ay=%u,%u,%u\n",
               fps_busy ? (uint16_t)((uint32_t)fps_drawn * 500u / fps_busy) : 0, fps_drawn, fps_busy, fps_ticks, screen,
               pixels_last,
               snd_reg(0, 8) & 15, snd_reg(1, 8) & 15, snd_reg(2, 8) & 15);
        fps_frames = fps_drawn = fps_ticks = fps_busy = 0;
    }
}
