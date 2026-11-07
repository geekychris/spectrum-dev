/*
 * Spectrum Arcade: the game menu that NextZXOS runs from the SD card.
 *
 * autoexec.bas (made by ./zx arcade) loads this at 32768 and calls it with
 * USR. It shows the games from /arcade/games.dat on Layer 2 (320 x 256): a
 * list on the left, the selected game's picture and description on the
 * right, sorted A-Z or by how often each was played (/arcade/stats.dat).
 * Choosing one stores its number at 32767 and returns to BASIC, which starts
 * it the way NextZXOS's browser would (.nexload, or SPECTRUM for a 48K
 * snapshot). Reset comes back here.
 *
 * Keys: Q/A, cursor keys, Sinclair or Kempston up/down to move; ENTER, SPACE
 * or fire to play; S switches the sort order; BREAK leaves to BASIC.
 */
#include <stdint.h>
#include <string.h>
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "listui.h"
#include "sdfile.h"
#include "zxlog.h"

#define MAX_GAMES  32
#define REC_SIZE   256
#define CHOICE     (*(volatile uint8_t *)32767)   /* read by autoexec.bas */
#define NO_GAME    255

/* games.dat: "ZXARC1", count, 0, then count records of REC_SIZE bytes */
typedef struct {
    char title[24];
    uint8_t machine;             /* 0 = 48K, 1 = Next */
    uint8_t pad[7];
    char controls[48];
    char desc[176];              /* lines separated by '\n' (wrapped by arcade.py) */
} game_rec;

typedef struct {
    uint16_t plays[MAX_GAMES];
    uint8_t sort;                /* 0 = A-Z, 1 = most played */
    uint8_t last;                /* game number last played */
} stats_t;

static uint8_t dat[8 + MAX_GAMES * REC_SIZE];
static game_rec *games;
static uint8_t ngames;
static stats_t stats;
static uint8_t order[MAX_GAMES];
static listui list;
static char pic_path[20];

/* colours, RRRGGGBB */
#define C_BG     0x01
#define C_TITLE  0xFC
#define C_TEXT   0xDB
#define C_DIM    0x6E
#define C_BAR    0x1F
#define C_BARFG  0x00
#define C_48K    0xF4
#define C_NEXT   0x5F
#define C_FRAME  0xB6

#define PIC_X 152
#define PIC_Y 56
#define PIC_W 160
#define PIC_H 120

static const char *label_of(uint8_t i) { return games[order[i]].title; }

static const char *tag_of(uint8_t i, uint8_t *colour)
{
    uint8_t next = games[order[i]].machine;
    *colour = next ? C_NEXT : C_48K;
    return next ? "Next" : "48K";
}

static void sort_games(void)
{
    uint8_t i, j;
    for (i = 0; i < ngames; i++) order[i] = i;
    for (i = 1; i < ngames; i++)            /* insertion sort: a handful of games */
        for (j = i; j > 0; j--) {
            uint8_t a = order[j - 1], b = order[j];
            int8_t c = 0;
            if (stats.sort) c = stats.plays[a] < stats.plays[b] ? 1 : stats.plays[a] > stats.plays[b] ? -1 : 0;
            if (!c) c = strcmp(games[a].title, games[b].title) > 0;
            if (c <= 0) break;
            order[j - 1] = b;
            order[j] = a;
        }
}

static void draw_footer(void)
{
    l2w_fill(16, 236, 288, 8, C_BG);
    l2w_text(16, 236, stats.sort ? "Q/A select  ENTER play  S sort: most played"
                                 : "Q/A select  ENTER play  S sort: A-Z", C_DIM, 1);
}

static void draw_frame(void)
{
    l2w_fill(0, 0, 320, 256, C_BG);
    l2w_text5(160 - 15 * 12 / 2, 16, "SPECTRUM ARCADE", C_TITLE, 2, 2, 1);
    l2w_fill(16, 36, 288, 1, C_FRAME);
    l2w_fill(PIC_X - 2, PIC_Y - 2, PIC_W + 4, PIC_H + 4, C_FRAME);
    l2w_fill(PIC_X, PIC_Y, PIC_W, PIC_H, 0);
    draw_footer();
}

/* the selected game's details, without the picture (that comes a moment later) */
static void draw_details(void)
{
    game_rec *g = &games[order[list.sel]];
    char line[28];
    const char *s = g->desc;
    uint8_t y = 182, n;
    l2w_fill(PIC_X - 2, 42, PIC_W + 4, 10, C_BG);
    l2w_text(PIC_X, 43, g->machine ? "Spectrum Next" : "ZX Spectrum 48K", g->machine ? C_NEXT : C_48K, 1);
    l2w_fill(PIC_X, PIC_Y, PIC_W, PIC_H, 0);
    l2w_fill(PIC_X - 2, 180, PIC_W + 8, 40, C_BG);
    while (*s && y < 218) {
        for (n = 0; *s && *s != '\n' && n < sizeof line - 1; n++) line[n] = *s++;
        line[n] = 0;
        if (*s == '\n') s++;
        l2w_text(PIC_X, y, line, C_TEXT, 1);
        y += 9;
    }
    l2w_fill(16, 222, 296, 8, C_BG);
    l2w_text(16, 222, g->controls, C_TITLE, 1);
}

static void show_picture(void)
{
    uint8_t g = order[list.sel];
    strcpy(pic_path, "/arcade/pic00.l2");
    pic_path[11] = '0' + g / 10;
    pic_path[12] = '0' + g % 10;
    if (!sd_load_l2w(pic_path, PIC_X, PIC_Y, PIC_W, PIC_H))
        l2w_text(PIC_X + 40, PIC_Y + 56, "no picture", C_DIM, 1);
    zx_log("arcade: shown=%u %s\n", g, games[g].title);
}

static void select_game(uint8_t g)
{
    uint8_t i;
    for (i = 0; i < ngames; i++)
        if (order[i] == g) listui_select(&list, i);
}

static void l2_identity_palette(void)
{
    uint16_t i;
    ZXN_WRITE_REG(0x43, 0x10);                  /* Layer 2 first palette, auto-increment */
    ZXN_WRITE_REG(0x40, 0);
    for (i = 0; i < 256; i++) ZXN_WRITE_REG(0x41, (uint8_t)i);
}

int main(void)
{
    uint8_t save7 = ZXN_READ_REG(0x07) & 3, save12 = ZXN_READ_REG(0x12), save15 = ZXN_READ_REG(0x15);
    uint8_t choice = NO_GAME, held = 0, pic_due = 0;
    int16_t n;

    CHOICE = NO_GAME;
    ZXN_WRITE_REG(0x07, 0x02);                  /* 28 MHz (see next_init) */
    ZXN_WRITE_REG(0x07, 0x03);
    frame_init();
    input_init();
    frame_hook = input_sample;
    input_latch(1);

    n = sd_load("/arcade/games.dat", dat, sizeof dat);
    games = (game_rec *)(dat + 8);
    ngames = (n > 8 && !memcmp(dat, "ZXARC1", 6)) ? dat[6] : 0;
    if (ngames > MAX_GAMES) ngames = MAX_GAMES;
    if (sd_load("/arcade/stats.dat", &stats, sizeof stats) != sizeof stats) memset(&stats, 0, sizeof stats);
    if (stats.sort > 1) stats.sort = 0;
    sort_games();

    ZXN_WRITE_REG(0x15, 0x00);                  /* S L U, no sprites */
    z80_outp(0xFE, 1);                          /* border: blue, like the menu */
    l2_identity_palette();
    l2w_init(9);
    draw_frame();

    list.x = 16; list.w = 128; list.y = 44; list.rows = 14; list.row_h = 12;
    list.count = ngames; list.top = 0; list.sel = 0;
    list.bg = C_BG; list.fg = C_TEXT; list.bar = C_BAR; list.bar_fg = C_BARFG;
    list.label = label_of; list.tag = tag_of;
    listui_draw(&list);
    if (!ngames) {
        l2w_text(PIC_X, 182, "No games in /arcade/games.dat", C_TEXT, 1);
        zx_log("arcade: no games\n");
    } else {
        select_game(stats.last < ngames ? stats.last : 0);
        draw_details();
        show_picture();
    }
    zx_log("arcade: ready games=%u sort=%u sel=%u\n", ngames, stats.sort, order[list.sel]);

    for (;;) {
        uint8_t in, pressed, moved = 0;
        char letter;
        frame_wait();
        in = input_read();
        pressed = input_pressed();
        letter = input_letter_pressed();
        if (in & IN_QUIT) break;
        if (!ngames) continue;
        /* up/down: a step per press, then auto-repeat while held */
        if (in & (IN_UP | IN_DOWN)) held++; else held = 0;
        if ((pressed & IN_UP) || ((in & IN_UP) && held > 15 && !(held & 3))) moved = listui_move(&list, -1);
        if ((pressed & IN_DOWN) || ((in & IN_DOWN) && held > 15 && !(held & 3))) moved = listui_move(&list, 1);
        if (moved) {
            draw_details();
            pic_due = 6;                         /* load the picture once the cursor rests */
            zx_log("arcade: sel=%u %s\n", order[list.sel], games[order[list.sel]].title);
        }
        if (pic_due && !--pic_due) show_picture();
        if (letter == 'S') {
            uint8_t g = order[list.sel];
            stats.sort ^= 1;
            sort_games();
            list.top = 0;
            listui_draw(&list);
            select_game(g);
            draw_footer();
            zx_log("arcade: sort=%u first=%u\n", stats.sort, order[0]);
        }
        if (pressed & (IN_FIRE | IN_START)) {
            choice = order[list.sel];
            break;
        }
    }

    if (choice != NO_GAME) {
        if (stats.plays[choice] < 0xFFFF) stats.plays[choice]++;
        stats.last = choice;
        zx_log("arcade: launch=%u %s plays=%u\n", choice, games[choice].title, stats.plays[choice]);
    } else {
        zx_log("arcade: quit\n");
    }
    sd_save("/arcade/stats.dat", &stats, sizeof stats);

    /* leave the machine as BASIC had it */
    frame_hook = 0;
    z80_outp(0x123B, 0x00);                     /* Layer 2 off */
    z80_outp(0xFE, 7);
    ZXN_WRITE_REG(0x70, 0x00);
    ZXN_WRITE_REG(0x1C, 0x01);                  /* Layer 2 clip back to 256 x 192 */
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 255);
    ZXN_WRITE_REG(0x18, 0);
    ZXN_WRITE_REG(0x18, 191);
    ZXN_WRITE_REG(0x12, save12);
    ZXN_WRITE_REG(0x15, save15);
    ZXN_WRITE_REG(0x07, save7);
    frame_exit();
    CHOICE = choice;
    return 0;
}
