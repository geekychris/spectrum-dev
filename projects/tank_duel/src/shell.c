/*
 * shell.c - Tank Duel's game flow (port of the original's game.c): what
 * touches the rules (new games, the ticks, the events' explosions,
 * messages and sounds), resident, so the banked front end (front.c) and
 * the rules' banks can both use it: it maps the rules in and puts the
 * front end back.
 */
#include <stdint.h>
#include <string.h>
#include "zxgame.h"
#include "zxlog.h"
#include "td.h"
#include "duel.h"
#include "front.h"
#include "shell.h"

/* ------------------------------------------------------------------ sound */

/* effects: channel, AY period (0: noise), volume, decay, sweep (the original's SFX_*) */
static const int8_t sfx_tab[][5] = {
    { 0, 0, 13, 2, 0 },         /* fire: a noise burst */
    { 1, 0, 10, 3, 0 },         /* their fire */
    { 2, 90, 12, 1, -2 },       /* lob: a rising whistle */
    { 1, 0, 15, 1, 0 },         /* boom */
    { 2, 110, 13, 2, 8 },       /* hit: a falling clank */
    { 2, 40, 12, 2, 2 },        /* glance: a high ring */
    { 2, 60, 10, 3, 3 },        /* bounce */
    { 0, 0, 15, 1, 0 },         /* hurt */
    { 1, 0, 12, 2, 0 },         /* crate */
};
static uint8_t jingle_t, jingle_k;
static const uint8_t *jingle;
static const uint8_t j_clear[] = { 72, 76, 79, 84, 0 };      /* C E G C, the original's arpeggio */
static const uint8_t j_win[] = { 79, 84, 91, 0 };
static const uint8_t j_over[] = { 64, 60, 55, 52, 48, 0 };   /* a falling tone */

void sfx(uint8_t k)
{
    if (k == SFX_CLEAR) { jingle = j_clear; jingle_k = 0; jingle_t = 0; return; }
    if (k == SFX_WIN) { jingle = j_win; jingle_k = 0; jingle_t = 0; return; }
    if (k == SFX_OVER) { jingle = j_over; jingle_k = 0; jingle_t = 0; return; }
    {
        const int8_t *s = sfx_tab[k];
        uint16_t period = s[1] ? (uint16_t)(uint8_t)s[1] * 8 : 0;
        ay_sfx((uint8_t)s[0], period, (uint8_t)s[2], (uint8_t)s[3], s[4]);
    }
}

/* once a frame */
void front_sound(void)
{
    if (jingle) {
        if (jingle_t == 0) {
            uint8_t n = jingle[jingle_k];
            if (!n) jingle = 0;
            else {
                ay_sfx(2, AY_NOTE(n), 13, jingle[jingle_k + 1] ? 0 : 1, 0);
                jingle_k++;
                jingle_t = 6;
            }
        } else if (--jingle_t == 0 && jingle && !jingle[jingle_k]) {
            ay_sfx(2, AY_NOTE(jingle[jingle_k - 1]), 12, 1, 0);
            jingle = 0;
        }
    }
    sound_tick();
}

/* ------------------------------------------------------------------ the game flow */

Duel du;
static uint32_t next_seed = 1;
uint8_t fire_lock, play_in;
uint8_t front_entering;                 /* the initials editor is up */
uint8_t hud_dirty;

void flash(const char *s, uint8_t pen)
{
    uint8_t n = (uint8_t)strlen(s);
    if (n > sizeof(du.flash) - 1) n = sizeof(du.flash) - 1;
    memcpy(du.flash, s, n);
    du.flash[n] = 0;
    du.flash_timer = 30;
    du.flash_pen = pen;
    hud_dirty = 1;
}

static void blast(fix x, fix z, int16_t y, uint8_t size, uint8_t pen)
{
    uint8_t k;
    for (k = 0; k < MAX_BLASTS; k++)
        if (du.blast[k].t <= 0) {
            du.blast[k].t = 10;
            du.blast[k].x = (int16_t)(x >> 16);
            du.blast[k].z = (int16_t)(z >> 16);
            du.blast[k].y = y;
            du.blast[k].size = size;
            du.blast[k].pen = pen;
            return;
        }
}

static void handle_events(void)
{
    TDEvent e;
    uint8_t quiet = du.state == DS_TITLE;
    while (td_pop_event(&game, &e)) {
        switch (e.type) {                       /* what is seen happens in the demo too */
        case TDE_KILL: blast(e.x, e.z, 20, 26, 30); break;
        case TDE_HIT: case TDE_GLANCE: case TDE_CRATE_HIT: blast(e.x, e.z, 20, 8, 27); break;
        case TDE_CRATE_BREAK: blast(e.x, e.z, 26, 22, 14); break;
        case TDE_SPLASH: blast(e.x, e.z, e.a == 2 ? 10 : 20, e.a == 2 ? 30 : 8, 30); break;
        case TDE_HURT: if (!quiet) du.hurt_timer = 6; break;
        }
        if (quiet) continue;
        switch (e.type) {
        case TDE_FIRE: sfx(SFX_FIRE); break;
        case TDE_EFIRE: sfx(SFX_EFIRE); break;
        case TDE_LOB: sfx(SFX_LOB); flash("INCOMING MORTAR", C_RED); break;
        case TDE_KILL:
            sfx(SFX_BOOM);
            flash(e.b == TK_SCOUT ? "SCOUT DOWN" : e.b == TK_MORTAR ? "MORTAR DOWN" : "BULWARK DOWN", C_GREEN);
            break;
        case TDE_HIT: sfx(SFX_HIT); break;
        case TDE_GLANCE: sfx(SFX_GLANCE); flash("GLANCED OFF THE SHIELD", C_CYAN); break;
        case TDE_BOUNCE: sfx(SFX_BOUNCE); break;
        case TDE_HURT: sfx(SFX_HURT); break;
        case TDE_SPLASH: if (e.a == 2) sfx(SFX_BOOM); break;
        case TDE_CRATE_HIT: case TDE_CRATE_BREAK: sfx(SFX_CRATE); break;
        case TDE_ROUND_CLEAR: sfx(SFX_CLEAR); break;
        case TDE_DEAD: sfx(SFX_OVER); break;
        case TDE_WIN: sfx(SFX_WIN); break;
        }
        if (e.type == TDE_KILL || e.type == TDE_DEAD || e.type == TDE_ROUND_CLEAR || e.type == TDE_ROUND ||
            e.type == TDE_WIN || e.type == TDE_HURT) {
            static const char *const nm[TDE_COUNT] = { "", "", "", "", "", "", "", "kill", "hurt", "", "", "",
                                                        "clear", "round", "dead", "win" };
            zx_log("td: %s score=%ld round=%d hp=%d\n", nm[e.type], (long)game.score, game.round, game.tk[0].hp);
        }
        hud_dirty = 1;
    }
}

void start_game(uint8_t round)
{
    map_rules();
    td_new_game(&game, next_seed++, round);
    du.state = DS_PLAY;
    du.paused = 0;
    du.help = 0;
    du.autoplay = 0;
    fire_lock = 1;
    memset(du.blast, 0, sizeof(du.blast));
    zx_log("td: start round=%u enemies=%d\n", round, td_enemies_left(&game));
    map_front();
    front_redraw();
}

void to_title(void)
{
    map_rules();
    du.state = DS_TITLE;
    td_new_game(&game, 777 + next_seed, 1);
    memset(du.blast, 0, sizeof(du.blast));
    zx_log("td: title\n");
    map_front();
    front_redraw();
}

void front_init(void)
{
    du.start_round = 1;
    hs_init(500);                               /* 2500 2000 1500 1000 500 */
    to_title();
}

/* one tick of the original's 25 (the rules' banks mapped) */
void front_tick(void)
{
    uint8_t k;
    if (du.help || du.paused || front_entering) return;
    if (du.state == DS_TITLE) {
        if (game.over || game.won) td_new_game(&game, game.seed * 1103515245UL + 12345UL, 1 + (uint8_t)(game.seed % 3));
        td_tick(&game, td_bot(&game));
    } else if (du.state == DS_PLAY) {
        td_tick(&game, du.autoplay ? td_bot(&game) : play_in);
    }
    handle_events();
    for (k = 0; k < MAX_BLASTS; k++) if (du.blast[k].t > 0) du.blast[k].t--;
    if (du.flash_timer > 0 && !--du.flash_timer) hud_dirty = 1;
    if (du.state_timer > 0 && !--du.state_timer) hud_dirty = 1;
    if (du.hurt_timer > 0) du.hurt_timer--;
    if (du.state == DS_PLAY && (game.over || game.won)) {
        du.state = DS_OVER;
        du.state_timer = 40;
        if (game.score > du.best) du.best = game.score;
        map_front();
        front_redraw();
        map_rules();
        zx_log("td: gameover score=%ld round=%d kills=%d won=%d\n", (long)game.score, game.round, game.kills, game.won);
    }
}

