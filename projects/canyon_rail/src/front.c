/*
 * front.c - Canyon Rail's shell (the original's game.c): input, the title
 * with the autopilot riding behind it, play, game over, the help pages,
 * sound and explosions for the events; the HUD and every screen's text on
 * the tilemap (render.c's 3D view never draws text). In bank 20 (code and
 * tables; the variables are resident: front.h).
 *
 * port: keys for the Spectrum (QAOP / cursor / Sinclair / Kempston, SPACE
 * fire, P pause, B autopilot, H help, BREAK the title); high scores with
 * initials (the original keeps only the best); the HUD in the border round
 * the view; the hurt flash is a red frame round the view.
 */
#include <stdint.h>
#include <string.h>
#include "zxgame.h"
#include "nexttile.h"
#include "zxlog.h"
#include "cr.h"
#include "front.h"
#include <arch/zxn.h>
#include "palfx.h"
#include "nextcopper.h"
#include "render.h"

/* tiles: 0 blank, 1 solid (pen 1), 32..127 the ROM font (pen 1, shadow pen
   2), 128.. the title's big letters */
#define T_SOLID 1
#define T_BIG   128
/* tilemap palette offsets: pen 1 their colour, pen 2 the shadow */
enum { C_WHITE, C_YELLOW, C_GREEN, C_RED, C_CYAN, C_GREY, C_FIRE, C_NODE, C_PANEL, C_COUNT };
static const uint16_t tile_col[C_COUNT] = { 0xFFF, 0xFE4, 0x6F6, 0xF33, 0x5EF, 0x99A, 0xFA3, 0xF5E, 0x123 };

/* the view (Layer 2) is tilemap cells 4..35 x 4..27 */
#define VC0 4
#define VR0 4

const char big_letters[] = "CANYORIL";          /* the title's letters, in tile order (main.c makes them) */
static hs_editor ed;
static int8_t ed_place;
static uint8_t drawn_state = 0xFF, drawn_help = 0xFF, drawn_page = 0xFF, hurt_shown;
static char hud_txt[6][16];
static uint8_t fire_lock;

static uint16_t rgb9(uint16_t c)
{
    return (uint16_t)(((((c >> 8) & 15) * 7 + 7) / 15) << 6 | ((((c >> 4) & 15) * 7 + 7) / 15) << 3 |
                      (((c & 15) * 7 + 7) / 15));
}

static void put(uint8_t col, uint8_t row, const char *s, uint8_t c)
{
    tm_print(col, row, s, 32, c);
}

static void centre(uint8_t row, const char *s, uint8_t c)
{
    put(20 - (uint8_t)(strlen(s) / 2), row, s, c);
}

/* a HUD field: redrawn only when it changes */
static void field(uint8_t slot, uint8_t col, uint8_t row, uint8_t w, const char *s, uint8_t c)
{
    if (!strcmp(hud_txt[slot], s)) return;
    strcpy(hud_txt[slot], s);
    tm_fill(col, row, w, 1, 0, 0);
    put(col, row, s, c);
}

static char *num(char *p, uint32_t v, uint8_t digits)        /* zero-padded */
{
    uint8_t i = digits;
    p[i] = 0;
    while (i--) { p[i] = '0' + (uint8_t)(v % 10); v /= 10; }
    return p + digits;
}

static char *unum(char *p, uint32_t v)
{
    char t[11];
    uint8_t i = 10;
    t[10] = 0;
    do { t[--i] = '0' + (uint8_t)(v % 10); v /= 10; } while (v);
    strcpy(p, t + i);
    return p + strlen(p);
}

void front_init(void)
{
    static uint8_t solid[32];
    uint8_t k;
    memset(solid, 0, 32);
    tm_def(0, solid, 1);                                /* blank */
    memset(solid, 0x11, 32);
    tm_def(T_SOLID, solid, 1);
    for (k = 0; k < C_COUNT; k++) {
        tm_pal(k, 1, rgb9(tile_col[k]));
        tm_pal(k, 2, 0);                                /* black shadow */
    }
    /* (the font: main.c, from the ROM before the banks are mapped) */
    rl.start_stage = 1;
    hs_init(10000);
    {
        static const char names[HS_ENTRIES][4] = { "CNY", "RAL", "ZXN", "AMI", "GA " };
        uint8_t k;
        for (k = 0; k < HS_ENTRIES; k++) memcpy(hs_table[k].name, names[k], 4);
    }
    front_to_title();
}

void front_to_title(void)
{
    rl.state = RS_TITLE;
    rl.title_t = 0;
    rl.paused = 0;
    rl.help = 0;
    memset(rl.burst, 0, sizeof(rl.burst));
    zx_log("canyon: title\n");
}

void front_started(void)
{
    rl.state = RS_PLAY;
    rl.paused = 0;
    rl.help = 0;
    rl.autoplay = 0;
    fire_lock = 1;
    memset(rl.burst, 0, sizeof(rl.burst));
    zx_log("canyon: start stage %u seed %lu\n", (unsigned)game.stage, (unsigned long)game.seed);
}

/* ---------- the display (set up once) ---------- */
/* the original's 32 pens (render.h's names; 12-bit RGB) */
static const uint16_t rail_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0xE96, 0x975, 0x864, 0x258, 0xB64, 0x843, 0xA75, 0x754, 0x632,
    0x889, 0x556, 0x6CD, 0x38A, 0x9F4, 0xF44, 0xFA8, 0x99B, 0x667, 0xF5E, 0xFA3, 0x000, 0x000,
    0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000
};

static void set_palette(void)
{
    static uint16_t pal[32];
    uint8_t k;
    for (k = 0; k < 32; k++) pal[k] = rgb9(rail_palette[k]);
    pal[PEN_SKY] = RGB9(7, 0, 6);               /* 0xE3: transparent, the copper's sky behind */
    palfx_load(PAL_L2, 0, 32, pal);
}

/* port: the view is drawn 96 rows high and shown 192: every other line the
   copper moves Layer 2's Y scroll (NextReg 0x17) back a row, so each row is
   shown twice (ScAIry Fun's technique). The sky pen is transparent; behind
   it the copper changes the fallback colour (NextReg 0x4A) down the screen:
   the original's sky gradient (plat_sky: 0x58C to 0xFCA over 130 of its 256
   lines, here 97 of 192) */
static uint8_t rgb8(uint16_t c)
{
    return (uint8_t)((((c >> 8) & 15) >> 1) << 5 | (((c >> 4) & 15) >> 1) << 2 | (c & 15) >> 2);
}

static void copper(void)
{
    uint16_t L;
    copper_begin();
    copper_move(0x17, 0);
    copper_move(0x4A, rgb8(0x58C));
    for (L = 2; L < VIEW_H; L += 2) {
        copper_wait(L - 1, 50);                 /* the end of the line before */
        copper_move(0x17, (uint8_t)(192 - L / 2));
        if (L < 97) {
            uint16_t r = 5 * (97 - L) + 15 * L, g = 8 * (97 - L) + 12 * L, b = 12 * (97 - L) + 10 * L;
            copper_move(0x4A, rgb8((r / 97) << 8 | (g / 97) << 4 | (b / 97)));
        }
    }
    copper_end();
}

/* the tilemap's font: the ROM's (main.c copied it to page 39), shadowed,
   as tiles 32..127; the title's big letters at three times, tiles 128.. */
static void fonts(void)
{
    const char *p;
    uint8_t c, t = 128;
    ZXN_WRITE_MMU3(39);
    for (c = 32; c < 128; c++) tm_glyphnx(c, (const uint8_t *)(0x6000 + (c - 32) * 8), 1, 1, 2, 1);
    for (p = big_letters; *p; p++, t += 9) tm_glyphnx(t, (const uint8_t *)(0x6000 + (*p - 32) * 8), 3, 1, 2, 2);
    ZXN_WRITE_MMU3(11);
}

void front_setup(void)
{
    tm_init();
    fonts();
    set_palette();
    copper();
}

/* ---------- input (the original's game_input) ---------- */
void front_input(uint8_t in, uint8_t edge, uint8_t keys)
{
    play_in = 0;
    if (keys & KEY_HELP && rl.state != RS_ENTRY) {
        rl.help = (rl.help + 1) % 3;
        zx_log("canyon: help %u\n", (unsigned)rl.help);
        return;
    }
    if (rl.help) {
        if (edge & (IN_FIRE | IN_START | IN_QUIT)) rl.help = 0;
        return;
    }
    if (edge & IN_QUIT && rl.state != RS_TITLE) {
        front_to_title();
        return;
    }
    switch (rl.state) {
    case RS_TITLE:
        if ((edge & IN_UP) && rl.start_stage < CR_STAGES) rl.start_stage++;
        if ((edge & IN_DOWN) && rl.start_stage > 1) rl.start_stage--;
        if (edge & (IN_FIRE | IN_START)) start_stage_req = rl.start_stage;
        break;
    case RS_PLAY:
        if (keys & KEY_PAUSE) {
            rl.paused = !rl.paused;
            zx_log(rl.paused ? "canyon: paused\n" : "canyon: resumed\n");
        }
        if (keys & KEY_AUTO) {
            rl.autoplay = !rl.autoplay;
            zx_log("canyon: autoplay %u\n", (unsigned)rl.autoplay);
        }
        play_in = (in & IN_LEFT ? CR_IN_LEFT : 0) | (in & IN_RIGHT ? CR_IN_RIGHT : 0) | (in & IN_UP ? CR_IN_UP : 0) |
                  (in & IN_DOWN ? CR_IN_DOWN : 0) | ((in & IN_FIRE) && !fire_lock ? CR_IN_FIRE : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case RS_OVER:
        if (rl.state_timer == 0 && (edge & (IN_FIRE | IN_START))) {
            ed_place = hs_rank((uint32_t)game.score);
            if (ed_place >= 0) {
                hs_edit_begin(&ed);
                rl.state = RS_ENTRY;
                zx_log("canyon: hiscore rank=%d\n", (int)ed_place);
            } else front_to_title();
        }
        break;
    case RS_ENTRY: {
        uint8_t r = hs_edit_step(&ed);
        if (r & HS_EDIT_TAKEN) sfx(SFX_KEY);
        if (r & HS_EDIT_CHANGED) drawn_state = 0xFF;
        if (r & HS_EDIT_DONE) {
            hs_insert(ed_place, ed.name, (uint32_t)game.score);
            zx_log("canyon: name=%s\n", ed.name);
            front_to_title();
        }
        break;
    }
    }
}

/* ---------- after a tick: events, timers (the original's game_update) ---------- */
static void add_burst(int32_t x, int32_t y, int32_t z, uint8_t size)
{
    uint8_t k;
    for (k = 0; k < MAX_BURSTS; k++)
        if (rl.burst[k].t <= 0) {
            rl.burst[k].t = 10; rl.burst[k].x = x; rl.burst[k].y = y; rl.burst[k].z = z;
            rl.burst[k].size = size;
            return;
        }
}

static void flash(const char *s, uint8_t pen)
{
    strncpy(rl.flash, s, sizeof(rl.flash) - 1);
    rl.flash_timer = 30;
    rl.flash_pen = pen;
}

static void events(void)
{
    CREvent e;
    uint8_t quiet = rl.state == RS_TITLE;
    int32_t x, y, z;
    while (cr_pop_event(&game, &e)) {
        switch (e.type) {                       /* what is seen happens in the demo too */
        case CRE_SHOT: rl.tracer_timer = 2; break;
        case CRE_KILL: case CRE_POD: cr_target_pos(&game, e.a, &x, &y, &z); add_burst(x, y, z, 30); break;
        case CRE_HIT:
            if (game.boss) cr_node_pos(&game, e.a, &x, &y, &z); else cr_target_pos(&game, e.a, &x, &y, &z);
            add_burst(x, y, z, 12);
            break;
        case CRE_BOLT_DOWN: cr_bolt_pos(&game, e.a, &x, &y, &z); add_burst(x, y, z, 14); break;
        case CRE_NODE: cr_node_pos(&game, e.a, &x, &y, &z); add_burst(x, y, z, 50); break;
        case CRE_HURT: if (!quiet) rl.hurt_timer = 6; break;
        }
        if (quiet) continue;
        switch (e.type) {
        case CRE_SHOT: sfx(SFX_SHOT); break;
        case CRE_KILL: case CRE_NODE: sfx(SFX_BOOM); break;
        case CRE_HIT: sfx(SFX_HIT); break;
        case CRE_BOLT_DOWN: sfx(SFX_BOLT); break;
        case CRE_FIRE: sfx(SFX_EFIRE); break;
        case CRE_HURT: sfx(SFX_HURT); break;
        case CRE_POD: sfx(SFX_POD); flash("SHIELD +25", C_GREEN); break;
        case CRE_BOSS: sfx(SFX_BOSS); break;
        case CRE_BOSS_DOWN: sfx(SFX_CLEAR); break;
        case CRE_OVER: sfx(SFX_OVER); break;
        case CRE_WIN: sfx(SFX_WIN); break;
        }
        if (e.type != CRE_SHOT && e.type != CRE_FIRE)
            zx_log("canyon: ev %s a=%d score=%ld stage=%u shield=%d\n", cr_event_names[e.type], e.a,
                   (long)game.score, (unsigned)game.stage, game.shield);
    }
}

void front_after_tick(void)
{
    uint8_t k;
    events();
    for (k = 0; k < MAX_BURSTS; k++) if (rl.burst[k].t > 0) rl.burst[k].t--;
    if (rl.flash_timer > 0) rl.flash_timer--;
    if (rl.state_timer > 0) rl.state_timer--;
    if (rl.hurt_timer > 0) rl.hurt_timer--;
    if (rl.tracer_timer > 0) rl.tracer_timer--;
    if (rl.state == RS_TITLE) rl.title_t++;
    if (rl.state == RS_PLAY && (game.over || game.won)) {
        rl.state = RS_OVER;
        rl.state_timer = 40;
        if (game.score > rl.best) rl.best = game.score;
        zx_log("canyon: state over score %ld won %u\n", (long)game.score, (unsigned)game.won);
    }
}

uint8_t front_scene(void)
{
    return !rl.help && rl.state != RS_ENTRY;
}

/* ---------- the tilemap ---------- */
static void big(uint8_t col, uint8_t row, const char *s, uint8_t c)
{
    for (; *s; s++, col += 3) {
        const char *p = strchr(big_letters, *s);
        uint8_t t, r, q, k;
        if (!p || *s == ' ') continue;
        t = T_BIG + (uint8_t)(p - big_letters) * 9;
        for (r = 0, k = 0; r < 3; r++)
            for (q = 0; q < 3; q++, k++) tm_put(col + q, row + r, t + k, c);
    }
}

static void hud(void)
{
    char tmp[16];
    uint8_t m = 1 + game.combo / 4, k, w;
    if (m > 8) m = 8;
    strcpy(tmp, "SCORE ");
    num(tmp + 6, (uint32_t)game.score, 7);
    field(0, VC0, 1, 13, tmp, C_WHITE);
    tmp[0] = 0;
    if (m > 1) { tmp[0] = 'x'; unum(tmp + 1, m); }
    field(1, 18, 1, 3, tmp, C_YELLOW);
    strcpy(tmp, "STAGE ");
    unum(tmp + 6, game.stage);
    field(2, 29, 1, 7, tmp, C_YELLOW);
    /* how far to the guardian (12 cells), or its nodes */
    if (!game.boss) {
        w = (uint8_t)(game.cam_s * 12 / CR_LEN);
        for (k = 0; k < 12; k++) tm_put(24 + k, 2, k < w ? T_SOLID : '-', k < w ? C_CYAN : C_GREY);
    } else
        for (k = 0; k < 12; k++)
            tm_put(24 + k, 2, (k % 3 == 2) ? 0 : T_SOLID, game.node_hp[k / 3] > 0 ? C_NODE : C_PANEL);
    put(VC0, 29, "SHIELD", C_GREY);
    w = (uint8_t)(game.shield * 12 / CR_SHIELD);
    for (k = 0; k < 12; k++) tm_put(11 + k, 29, k < w ? T_SOLID : '-', game.shield > 30 ? C_GREEN : C_RED);
    field(3, 31, 29, 4, rl.autoplay ? "AUTO" : "", C_GREEN);
}

static void frame_red(uint8_t on)
{
    uint8_t t = on ? T_SOLID : 0;
    tm_fill(VC0 - 1, VR0 - 1, 34, 1, t, C_RED);
    tm_fill(VC0 - 1, VR0 + 24, 34, 1, t, C_RED);
    tm_fill(VC0 - 1, VR0, 1, 24, t, C_RED);
    tm_fill(VC0 + 32, VR0, 1, 24, t, C_RED);
}

static const char *const help_lines[2][12] = {
    { "THE RIDE TAKES YOU DOWN THE",
      "CANYON: MOVE THE SIGHT, FIRE.",
      "",
      "TURRETS  ON THE WALLS, 2 HITS",
      "DRONES   SWOOP ACROSS, 1 HIT",
      "BOLTS    THEIR SHOTS: SHOOT THEM",
      "         OR YOUR SHIELD TAKES THEM",
      "PODS     SHOOT THEM: +25 SHIELD",
      "",
      "HITS IN A ROW BUILD A MULTIPLIER",
      "UP TO x8. THEN THE GUARDIAN: SHOOT",
      "OUT ITS FOUR NODES. THREE STAGES." },
    { "QAOP/CURSOR/STICK  MOVE THE SIGHT",
      "SPACE / FIRE       FIRE (HOLD)",
      "P                  PAUSE",
      "B                  AUTOPILOT",
      "H                  THESE PAGES",
      "BREAK              THE TITLE",
      "",
      "TURRET 100, DRONE 150, BOLT 25,",
      "POD 50, NODE 500,",
      "GUARDIAN 2000 x STAGE.",
      "ALL x THE MULTIPLIER; A MISS OR",
      "A HIT TAKEN RESETS IT." },
};

static void screen_help(uint8_t page)
{
    uint8_t k;
    tm_clear();
    centre(5, page == 1 ? "HOW TO PLAY" : "CONTROLS", C_YELLOW);
    for (k = 0; k < 12; k++) put(4, 8 + k, help_lines[page - 1][k], C_WHITE);
    put(4, 25, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2", C_GREY);
    put(18, 26, "H: NEXT  FIRE: BACK", C_YELLOW);
}

static void screen_title(uint8_t page)
{
    char tmp[40], *p;
    uint8_t k;
    tm_clear();
    big(11, 5, "CANYON", C_YELLOW);
    big(14, 9, "RAIL", C_FIRE);
    if (page == 0) {
        p = tmp;
        strcpy(p, "START AT STAGE ");
        unum(p + 15, rl.start_stage);
        strcat(p, " (UP/DOWN)");
        centre(21, tmp, C_GREEN);
        centre(23, "PRESS FIRE TO RIDE", C_YELLOW);
        strcpy(tmp, "BEST ");
        unum(tmp + 5, hs_table[0].score);
        strcat(tmp, "   H: HOW TO PLAY");
        centre(25, tmp, C_GREY);
    } else {
        centre(19, "BEST RIDERS", C_YELLOW);
        for (k = 0; k < HS_ENTRIES; k++) {
            p = tmp;
            *p++ = '1' + k;
            *p++ = ' ';
            strcpy(p, hs_table[k].name);
            p += strlen(p);
            *p++ = ' ';
            num(p, hs_table[k].score, 7);
            centre(20 + k, tmp, k ? C_WHITE : C_CYAN);
        }
    }
}

static void screen_entry(void)
{
    char tmp[4];
    tm_clear();
    centre(9, "A NEW BEST RIDER", C_YELLOW);
    centre(12, "TYPE YOUR INITIALS", C_WHITE);
    centre(14, "OR UP / DOWN AND FIRE", C_GREY);
    tmp[0] = ed.name[0]; tmp[1] = ed.name[1]; tmp[2] = ed.name[2]; tmp[3] = 0;
    put(18, 18, tmp, C_CYAN);
    tm_fill(18, 19, 3, 1, 0, 0);
    put(18 + ed.pos, 19, "^", C_YELLOW);
}

static void screen_play(void)
{
    char tmp[40];
    /* the messages over the view, rows 8..26 (cleared each time) */
    tm_fill(VC0, VR0 + 4, 32, 19, 0, 0);
    if (rl.flash_timer > 0 && rl.flash[0]) centre(VR0 + 4, rl.flash, rl.flash_pen);
    if (game.between) {
        centre(VR0 + 7, "STAGE CLEAR", C_GREEN);
        if (game.stage < CR_STAGES) {
            strcpy(tmp, "STAGE ");
            unum(tmp + 6, game.stage + 1);
            strcat(tmp, " NEXT");
            centre(VR0 + 9, tmp, C_WHITE);
        }
    } else if (game.boss && game.tick % 50 < 30 &&
               game.node_hp[0] + game.node_hp[1] + game.node_hp[2] + game.node_hp[3] == 4 * (3 + game.stage))
        centre(VR0 + 6, "GUARDIAN: SHOOT ITS NODES", C_RED);
    if (rl.state == RS_OVER) {
        tm_fill(VC0 + 5, VR0 + 8, 22, 9, T_SOLID, C_PANEL);
        centre(VR0 + 9, game.won ? "VICTORY" : "GAME OVER", game.won ? C_GREEN : C_RED);
        strcpy(tmp, "HITS ");
        unum(tmp + 5, game.shots ? (uint32_t)game.hits * 100 / game.shots : 0);
        strcat(tmp, "% CHAIN ");
        unum(tmp + strlen(tmp), game.best_combo);
        centre(VR0 + 11, tmp, C_YELLOW);
        strcpy(tmp, "SCORE ");
        unum(tmp + 6, (uint32_t)game.score);
        centre(VR0 + 13, tmp, C_WHITE);
        if (rl.state_timer == 0) centre(VR0 + 15, "PRESS FIRE", C_YELLOW);
    }
    if (rl.paused) centre(VR0 + 21, "PAUSED", C_WHITE);
}

void front_draw(void)
{
    uint8_t page = rl.state == RS_TITLE ? (uint8_t)((rl.title_t / 150) & 1) : 0;
    uint8_t whole = rl.state != drawn_state || rl.help != drawn_help || page != drawn_page;
    if (whole) {
        drawn_state = rl.state;
        drawn_help = rl.help;
        drawn_page = page;
        memset(hud_txt, 1, sizeof(hud_txt));
        hurt_shown = 0;
        if (rl.help) { screen_help(rl.help); return; }
        if (rl.state == RS_TITLE) { screen_title(page); return; }
        if (rl.state == RS_ENTRY) { screen_entry(); return; }
        tm_clear();
    }
    if (rl.help || rl.state == RS_TITLE || rl.state == RS_ENTRY) return;
    hud();
    screen_play();
    if ((rl.hurt_timer > 0) != hurt_shown) {
        hurt_shown = rl.hurt_timer > 0;
        frame_red(hurt_shown);
    }
}
