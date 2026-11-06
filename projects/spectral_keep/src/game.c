/*
 * The game around the rooms (Game.cs): title, keeps, lives, keys, relics,
 * messages, moving between rooms, dying and winning. 50 logic steps/s.
 * The original's game.c; port: changes are marked.
 */
#include <string.h>
#include "keep.h"

Game G;

#define START_LIVES 4
#define MAXROOMS    10              /* port: 16; a keep has at most 10 rooms */
static uint8_t taken[MAXROOMS][(MAXH * MAXW * MAXW + 7) / 8];   /* port: a bit per cell */
static uint8_t gate_open[MAXROOMS];

/* port: no sprintf (z88dk's is big); strings are built with these */
static char *put_str(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_num(char *p, int16_t v)
{
    char t[6];
    uint8_t n = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static void set_state(uint8_t s)
{
    G.state = s;
    G.timer = 0;
    keep_log("state=%u level=%u room=%s lives=%d relics=%d\n", s, G.level_index,
             R.room ? R.room->id : "-", G.lives, G.relics);
}

static void say(const char *text, uint8_t ink, int16_t ticks)
{
    strncpy(G.message, text ? text : "", sizeof(G.message) - 1);
    G.message[sizeof(G.message) - 1] = 0;
    G.msg_ink = ink;
    G.msg_until = G.clock + ticks;
    G.msg_changed = 1;
}

uint8_t game_message_visible(void) { return G.clock < G.msg_until && G.message[0]; }

static void flash(uint8_t ink, int16_t ticks)
{
    G.flash_ink = ink;
    G.flash_until = G.clock + ticks;
    G.flash_len = ticks;
}

static void enter_room(uint8_t ridx, fix x, fix y, fix z)
{
    room_build(G.level, ridx, 1);
    room_spawn_player(x, y, z);
    G.entry_x = x;
    G.entry_y = y;
    G.entry_z = z;
    G.room_changed = 1;
    if (!(G.visited[ridx >> 3] & (1 << (ridx & 7)))) {
        G.visited[ridx >> 3] |= (uint8_t)(1 << (ridx & 7));
        G.nvisited++;
        keep_log("room %s visited=%u/%u\n", R.room->id, G.nvisited, G.level->nrooms);
    }
}

static void preview_selected(void)
{
    const KLevel *l = &keep_levels[G.selected];
    G.level = l;
    room_build(l, l->start, 0);
    G.room_changed = 1;
}

static void to_title(void)
{
    G.paused = 0;
    set_state(GS_TITLE);
    snd_music(T_TITLE);
    preview_selected();
}

static void start_level(uint8_t index)
{
    const KLevel *l = &keep_levels[index];
    G.level_index = index;
    G.level = l;
    if (index == 0 || G.lives <= 0) G.lives = START_LIVES;
    G.keys = G.relics = 0;
    memset(taken, 0, sizeof(taken));
    memset(gate_open, 0, sizeof(gate_open));
    memset(G.visited, 0, sizeof(G.visited));
    G.nvisited = 0;
    G.msg_until = 0;
    G.msg_changed = 1;
    G.last_nag = -1000;
    keep_log("level %s rooms=%u relics=%u need=%u\n", l->name, l->nrooms, l->nrelics, l->relics);
    enter_room(l->start, ((fix)l->sx << FXS) + FX / 2, (fix)l->sy << FXS, ((fix)l->sz << FXS) + FX / 2);
    snd_music(T_GATEHOUSE + index % 3);
    set_state(GS_PLAYING);
    snd_play(SFX_START);
}

static void respawn(void)
{
    if (G.lives <= 0) {
        snd_music(T_NONE);
        set_state(GS_GAMEOVER);
        snd_play(SFX_DEATH);
        return;
    }
    enter_room(R.ridx, G.entry_x, G.entry_y, G.entry_z);
    R.act[R.player].invuln = 75;          /* 1.5 s */
    set_state(GS_PLAYING);
}

static void next_level(void)
{
    if (G.level_index + 1 < keep_nlevels) {
        int8_t lives = G.lives;
        start_level(G.level_index + 1);
        G.lives = lives;
    } else {
        snd_music(T_TITLE);
        set_state(GS_VICTORY);
        snd_play(SFX_LEVELDONE);
    }
}

/* ---- what the room reports ---- */

uint8_t host_taken(uint8_t ridx, uint16_t cell)
{
    return ridx < MAXROOMS && (taken[ridx][cell >> 3] & (1 << (cell & 7)));
}
uint8_t host_gate_open(uint8_t ridx) { return ridx < MAXROOMS && gate_open[ridx]; }
void host_sound(uint8_t sfx) { snd_play(sfx); }

void host_pickup(uint8_t kind, uint8_t ridx, uint16_t cell)
{
    char buf[48], *p;
    if (ridx < MAXROOMS) taken[ridx][cell >> 3] |= (uint8_t)(1 << (cell & 7));
    switch (kind) {
    case 'R':
        G.relics++;
        if (G.relics >= G.level->relics) strcpy(buf, "ALL RELICS FOUND! TO THE THRONE!");
        else {
            p = put_num(put_str(buf, "A RELIC! "), G.relics);
            put_num(put_str(p, " OF "), G.level->relics);
        }
        say(buf, INK_YELLOW, 175);
        snd_duck(40);
        snd_play(SFX_RELIC);
        flash(INK_YELLOW, 13);
        break;
    case 'P':
        G.lives++;
        say("A POTION. EXTRA LIFE!", INK_MAGENTA, 175);
        snd_play(SFX_PICKUP);
        flash(INK_MAGENTA, 10);
        break;
    case 'K':
        G.keys++;
        say("YOU FOUND A KEY", INK_CYAN, 175);
        snd_play(SFX_KEY);
        flash(INK_CYAN, 10);
        break;
    }
    keep_log("pickup %c %s:%u relics=%d keys=%d lives=%d\n", kind, R.room->id, cell, G.relics, G.keys, G.lives);
}

uint8_t host_try_key(uint8_t ridx)
{
    if (G.keys <= 0) {
        if (G.clock - G.last_nag > 150) {
            say("THE GATE IS LOCKED. FIND A KEY.", INK_RED, 175);
            G.last_nag = G.clock;
        }
        return 0;
    }
    G.keys--;
    if (ridx < MAXROOMS) gate_open[ridx] = 1;
    say("THE GATE GRINDS OPEN", INK_CYAN, 175);
    snd_play(SFX_GATE);
    keep_log("gate %s\n", R.room->id);
    return 1;
}

void host_gate_opened(void) { G.room_changed = 1; }   /* port: redraw the room without its gates */

void host_death(const char *cause)
{
    char buf[48];
    if (G.state != GS_PLAYING) return;
    G.lives--;
    put_str(put_str(put_str(buf, "KILLED BY "), cause), "!");
    say(buf, INK_RED, 125);
    snd_duck(80);
    snd_play(SFX_DEATH);
    flash(INK_RED, 30);
    keep_log("death %s room=%s lives=%d\n", cause, R.room->id, G.lives);
    set_state(GS_DYING);
}

void host_exit(uint8_t side, fix x, fix y, fix z)
{
    int8_t next = lv_neighbour(G.level, R.ridx, side);
    if (next < 0) return;
    room_entry_point(R.room, &G.level->rooms[next], side, &x, &y, &z);
    enter_room((uint8_t)next, x, y, z);
    snd_play(SFX_DOOR);
}

void host_throne(void)
{
    if (G.state != GS_PLAYING) return;
    if (G.relics >= G.level->relics) {
        say("THE KEEP IS YOURS!", INK_YELLOW, 250);
        snd_duck(175);
        snd_play(SFX_LEVELDONE);
        flash(INK_YELLOW, 75);
        keep_log("throne: keep %u done\n", G.level_index);
        set_state(GS_LEVELDONE);
    } else if (G.clock - G.last_nag > 150) {
        char buf[48];
        int8_t need = G.level->relics - G.relics;
        put_str(put_num(put_str(buf, "BRING "), need), need == 1 ? " MORE RELIC" : " MORE RELICS");
        say(buf, INK_YELLOW, 175);
        G.last_nag = G.clock;
    }
}

void host_talk(const char *text)
{
    say(text, INK_CYAN, 300);
    snd_play(SFX_TALK);
}

/* ---- the loop ---- */

void game_init(void)
{
    memset(&G, 0, sizeof(G));
    G.music_on = 1;
    G.lives = 0;
    to_title();
}

void game_step(uint16_t held, uint16_t pressed)
{
    G.clock++;
    G.timer++;
    switch (G.state) {
    case GS_TITLE:
        /* L+R: tour every room of every keep (Game.Tour), again to stop */
        if ((held & (B_L | B_R)) == (B_L | B_R) && (pressed & (B_L | B_R))) {
            G.tour = !G.tour;
            G.tour_room = -1;
            G.tour_level = 0;
            G.timer = 1000;
            if (!G.tour) preview_selected();
        }
        if (G.tour) {
            if (G.timer >= 150) {
                G.timer = 0;
                if (++G.tour_room >= keep_levels[G.tour_level].nrooms) {
                    G.tour_room = 0;
                    G.tour_level = (G.tour_level + 1) % keep_nlevels;
                }
                G.level = &keep_levels[G.tour_level];
                room_build(G.level, (uint8_t)G.tour_room, 0);
                G.room_changed = 1;
                keep_log("tour %s %s\n", G.level->name, R.room->id);
            }
            room_tick(0, 0, 0);
            if (pressed & (B_A | B_P)) { G.tour = 0; preview_selected(); }
            break;
        }
        if (pressed & B_DOWN) { G.selected = (G.selected + 1) % keep_nlevels; snd_play(SFX_BLIP); preview_selected(); }
        if (pressed & B_UP) { G.selected = (G.selected + keep_nlevels - 1) % keep_nlevels; snd_play(SFX_BLIP); preview_selected(); }
        if (pressed & B_C) { G.grid_controls = !G.grid_controls; snd_play(SFX_BLIP); }
        if (pressed & B_B) { G.music_on = !G.music_on; snd_music_enable(G.music_on); snd_play(SFX_BLIP); }
        if (pressed & (B_A | B_P)) { G.lives = 0; start_level(G.selected); return; }
        room_tick(0, 0, 0);
        break;
    case GS_PLAYING:
        if (pressed & B_P) {
            G.paused = !G.paused;
            keep_log("paused=%u\n", G.paused);
        }
        if (G.paused) { G.timer--; G.clock--; return; }
        {
            int16_t h = 0, v = 0, mx, mz;
            if (held & B_LEFT) h -= D12;
            if (held & B_RIGHT) h += D12;
            if (held & B_UP) v += D12;
            if (held & B_DOWN) v -= D12;
            if (G.grid_controls) { mx = h; mz = v; }
            else {                                          /* screen-relative */
                mx = (int16_t)(((int32_t)(h + v) * 2896) >> 12);
                mz = (int16_t)(((int32_t)(v - h) * 2896) >> 12);
            }
            room_tick(mx, mz, (pressed & (B_A | B_B)) != 0);
        }
        break;
    case GS_DYING:
        if (G.timer > 65) respawn();
        break;
    case GS_LEVELDONE:
        if (G.timer > 160) next_level();
        break;
    case GS_GAMEOVER:
    case GS_VICTORY:
        if ((G.timer > 50 && (pressed & (B_A | B_P))) || G.timer > 400) to_title();
        break;
    }
}
