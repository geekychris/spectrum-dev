/*
 * The original's drawing functions, copied verbatim from
 * amiga_mcp/examples/jump_quest/src (gfx.c, player.c, enemy.c, items.c, hud.c,
 * title.c). tools/gen_gfx.py builds them on the host over amiga_shim.h (a
 * RastPort that draws into a byte per pixel) and turns what they draw into
 * the Spectrum's tiles and sprites (src/gfx_data.c).
 */
#include "amiga_shim.h"

/* ---- gfx.c */
void gfx_draw_tile(struct RastPort *rp, int tile_id, int sx, int sy) {
    int ex = sx + TILE_SIZE - 1;
    int ey = sy + TILE_SIZE - 1;

    /* Skip tiles that are partially off-screen to avoid draw artifacts */
    if (sx < 0 || ex >= SCREEN_W || sy < 0 || ey >= HUD_Y) return;

    switch (tile_id) {
    case TILE_EMPTY:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        break;

    case TILE_GROUND:
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_DKBROWN);
        Move(rp, sx, sy + 4); Draw(rp, ex, sy + 4);
        Move(rp, sx + 4, sy + 8); Draw(rp, ex, sy + 8);
        Move(rp, sx, sy + 12); Draw(rp, sx + 8, sy + 12);
        break;

    case TILE_GRASS:
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx, sy + 4, ex, ey);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx, sy, ex, sy + 3);
        SetAPen(rp, COL_DKGREEN);
        RectFill(rp, sx + 2, sy, sx + 3, sy + 1);
        RectFill(rp, sx + 8, sy, sx + 9, sy + 1);
        RectFill(rp, sx + 13, sy, sx + 14, sy + 1);
        break;

    case TILE_BRICK:
        SetAPen(rp, COL_ORANGE);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_DKORANGE);
        Move(rp, sx, sy + 7); Draw(rp, ex, sy + 7);
        Move(rp, sx + 8, sy); Draw(rp, sx + 8, sy + 6);
        Move(rp, sx, sy + 15); Draw(rp, ex, sy + 15);
        Move(rp, sx + 4, sy + 8); Draw(rp, sx + 4, sy + 14);
        Move(rp, sx + 12, sy + 8); Draw(rp, sx + 12, sy + 14);
        break;

    case TILE_QBLOCK:
        /* gift box: red with a yellow ribbon */
        SetAPen(rp, COL_RED);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_BLACK);
        Move(rp, sx, sy); Draw(rp, ex, sy);
        Move(rp, sx, ey); Draw(rp, ex, ey);
        Move(rp, sx, sy); Draw(rp, sx, ey);
        Move(rp, ex, sy); Draw(rp, ex, ey);
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, sx + 7, sy + 1, sx + 8, ey - 1);
        RectFill(rp, sx + 1, sy + 7, ex - 1, sy + 8);
        RectFill(rp, sx + 4, sy + 2, sx + 6, sy + 4);
        RectFill(rp, sx + 9, sy + 2, sx + 11, sy + 4);
        break;

    case TILE_QBLOCK_HIT:
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_DKBROWN);
        Move(rp, sx, sy); Draw(rp, ex, sy);
        Move(rp, sx, ey); Draw(rp, ex, ey);
        Move(rp, sx, sy); Draw(rp, sx, ey);
        Move(rp, ex, sy); Draw(rp, ex, ey);
        break;

    case TILE_STONE:
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_BLACK);
        Move(rp, sx, sy); Draw(rp, ex, sy);
        Move(rp, sx, ey); Draw(rp, ex, ey);
        Move(rp, sx, sy); Draw(rp, sx, ey);
        Move(rp, ex, sy); Draw(rp, ex, ey);
        SetAPen(rp, COL_WHITE);
        Move(rp, sx + 1, sy + 1); Draw(rp, ex - 1, sy + 1);
        Move(rp, sx + 1, sy + 1); Draw(rp, sx + 1, ey - 1);
        break;

    /* "pipes" are stone columns with a capital */
    case TILE_PIPE_TL:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx, sy + 2, ex, sy + 5);
        RectFill(rp, sx + 3, sy + 6, ex, ey);
        SetAPen(rp, COL_WHITE);
        Move(rp, sx, sy + 2); Draw(rp, ex, sy + 2);
        Move(rp, sx + 5, sy + 7); Draw(rp, sx + 5, ey);
        SetAPen(rp, COL_BLACK);
        Move(rp, sx, sy + 5); Draw(rp, ex, sy + 5);
        break;

    case TILE_PIPE_TR:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx, sy + 2, ex, sy + 5);
        RectFill(rp, sx, sy + 6, ex - 3, ey);
        SetAPen(rp, COL_WHITE);
        Move(rp, sx, sy + 2); Draw(rp, ex, sy + 2);
        SetAPen(rp, COL_BLACK);
        Move(rp, sx, sy + 5); Draw(rp, ex, sy + 5);
        Move(rp, sx + 10, sy + 7); Draw(rp, sx + 10, ey);
        break;

    case TILE_PIPE_BL:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, sx + 2, ey);
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx + 3, sy, ex, ey);
        SetAPen(rp, COL_WHITE);
        Move(rp, sx + 5, sy); Draw(rp, sx + 5, ey);
        SetAPen(rp, COL_BLACK);
        Move(rp, sx + 3, sy); Draw(rp, sx + 3, ey);
        break;

    case TILE_PIPE_BR:
        SetAPen(rp, COL_SKY);
        RectFill(rp, ex - 2, sy, ex, ey);
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx, sy, ex - 3, ey);
        SetAPen(rp, COL_BLACK);
        Move(rp, sx + 10, sy); Draw(rp, sx + 10, ey);
        Move(rp, ex - 3, sy); Draw(rp, ex - 3, ey);
        break;

    case TILE_PLATFORM:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx, sy, ex, sy + 3);
        SetAPen(rp, COL_DKBROWN);
        Move(rp, sx, sy + 3); Draw(rp, ex, sy + 3);
        break;

    case TILE_CLOUD_L:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx + 4, sy + 4, ex, sy + 12);
        RectFill(rp, sx + 8, sy + 2, ex, sy + 4);
        break;

    case TILE_CLOUD_R:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx, sy + 4, sx + 12, sy + 12);
        RectFill(rp, sx, sy + 2, sx + 8, sy + 4);
        break;

    case TILE_FLAG:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx + 7, sy, sx + 8, ey);
        SetAPen(rp, COL_RED);
        RectFill(rp, sx + 9, sy + 1, sx + 15, sy + 6);
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, sx + 6, sy, sx + 9, sy + 1);
        break;

    case TILE_BUSH:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx + 2, sy + 6, sx + 14, sy + 14);
        RectFill(rp, sx + 4, sy + 4, sx + 12, sy + 6);
        SetAPen(rp, COL_DKGREEN);
        RectFill(rp, sx + 6, sy + 8, sx + 10, sy + 10);
        break;

    default:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        break;
    }
}

/* ---- player.c */
void player_draw(struct RastPort *rp, Player *p, int cam_x) {
    int sx = p->x - cam_x;
    int sy = p->y;
    int bw = (p->character == CHAR_RJ) ? 10 : 12; /* body width */
    int ox = (PLAYER_W - bw) / 2; /* center offset */
    int leg_off = 0;

    /* Off screen check */
    if (sx + PLAYER_W < 0 || sx >= SCREEN_W) return;

    /* Invulnerability flash */
    if (p->invuln > 0 && (p->invuln & 4)) return;

    /* Walking leg offset for animation */
    if (p->state == 1) {
        static const int leg_anim[] = { 0, 2, 0, -2 };
        leg_off = leg_anim[p->walk_frame & 3];
    }

    if (p->character == CHAR_RJ) {
        /* RJ - tall, slender */
        /* Hair (grey) */
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx + ox + 1, sy, sx + ox + bw - 2, sy + 2);

        /* Face (skin) */
        SetAPen(rp, COL_SKIN);
        RectFill(rp, sx + ox + 1, sy + 3, sx + ox + bw - 2, sy + 7);

        /* Eyes */
        SetAPen(rp, COL_BLACK);
        if (p->dir == DIR_RIGHT) {
            RectFill(rp, sx + ox + 5, sy + 4, sx + ox + 6, sy + 5);
        } else {
            RectFill(rp, sx + ox + 3, sy + 4, sx + ox + 4, sy + 5);
        }

        /* Beard (dark grey) */
        SetAPen(rp, COL_BLACK);
        RectFill(rp, sx + ox + 2, sy + 7, sx + ox + bw - 3, sy + 8);

        /* T-shirt (white) */
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx + ox, sy + 9, sx + ox + bw - 1, sy + 17);

        /* Jeans (blue) */
        SetAPen(rp, COL_BLUE);
        RectFill(rp, sx + ox, sy + 18, sx + ox + bw / 2 - 1 + leg_off, sy + 24);
        RectFill(rp, sx + ox + bw / 2 + 1 - leg_off, sy + 18,
                 sx + ox + bw - 1, sy + 24);

        /* Shoes (brown) */
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx + ox, sy + 25, sx + ox + bw / 2 - 1 + leg_off, sy + 27);
        RectFill(rp, sx + ox + bw / 2 + 1 - leg_off, sy + 25,
                 sx + ox + bw - 1, sy + 27);
    } else {
        /* Dale - shorter, rounder */
        /* Hair (grey) */
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx + ox, sy, sx + ox + bw - 1, sy + 2);

        /* Face (skin) */
        SetAPen(rp, COL_SKIN);
        RectFill(rp, sx + ox, sy + 3, sx + ox + bw - 1, sy + 7);

        /* Eyes */
        SetAPen(rp, COL_BLACK);
        if (p->dir == DIR_RIGHT) {
            RectFill(rp, sx + ox + 6, sy + 4, sx + ox + 7, sy + 5);
        } else {
            RectFill(rp, sx + ox + 4, sy + 4, sx + ox + 5, sy + 5);
        }

        /* Beard */
        SetAPen(rp, COL_BLACK);
        RectFill(rp, sx + ox + 1, sy + 7, sx + ox + bw - 2, sy + 8);

        /* T-shirt (white, wider) */
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx + ox - 1, sy + 9, sx + ox + bw, sy + 15);

        /* Jeans (blue, wider) */
        SetAPen(rp, COL_BLUE);
        RectFill(rp, sx + ox, sy + 16, sx + ox + bw / 2 - 1 + leg_off, sy + 20);
        RectFill(rp, sx + ox + bw / 2 + 1 - leg_off, sy + 16,
                 sx + ox + bw - 1, sy + 20);

        /* Shoes */
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx + ox, sy + 21, sx + ox + bw / 2 - 1 + leg_off, sy + 23);
        RectFill(rp, sx + ox + bw / 2 + 1 - leg_off, sy + 21,
                 sx + ox + bw - 1, sy + 23);
    }
}

/* ---- enemy.c */
void enemies_draw(struct RastPort *rp, int cam_x) {
    int i, sx, sy;
    Enemy *e;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &game.enemies[i];
        if (!e->alive) continue;

        sx = e->x - cam_x;
        sy = e->y;

        if (sx < 0 || sx + 16 > SCREEN_W) continue;
        if (sy < 0 || sy + 16 > HUD_Y) continue;

        switch (e->type) {
        case ENT_BEETLE:
            /* Brown beetle body */
            SetAPen(rp, COL_BROWN);
            RectFill(rp, sx + 2, sy + 4, sx + 13, sy + 12);
            /* Head */
            SetAPen(rp, COL_DKBROWN);
            RectFill(rp, sx + 4, sy + 1, sx + 11, sy + 4);
            /* Legs */
            SetAPen(rp, COL_DKBROWN);
            if (e->frame) {
                RectFill(rp, sx, sy + 8, sx + 2, sy + 14);
                RectFill(rp, sx + 13, sy + 6, sx + 15, sy + 14);
            } else {
                RectFill(rp, sx, sy + 6, sx + 2, sy + 14);
                RectFill(rp, sx + 13, sy + 8, sx + 15, sy + 14);
            }
            /* Eyes */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 5, sy + 2, sx + 6, sy + 3);
            RectFill(rp, sx + 9, sy + 2, sx + 10, sy + 3);
            break;

        case ENT_FLY:
            /* Grey body */
            SetAPen(rp, COL_GREY);
            RectFill(rp, sx + 4, sy + 6, sx + 11, sy + 13);
            /* Wings */
            SetAPen(rp, COL_WHITE);
            if (e->frame) {
                RectFill(rp, sx + 1, sy + 1, sx + 6, sy + 5);
                RectFill(rp, sx + 9, sy + 1, sx + 14, sy + 5);
            } else {
                RectFill(rp, sx + 1, sy + 3, sx + 6, sy + 6);
                RectFill(rp, sx + 9, sy + 3, sx + 14, sy + 6);
            }
            /* Eyes */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 5, sy + 7, sx + 6, sy + 8);
            RectFill(rp, sx + 9, sy + 7, sx + 10, sy + 8);
            break;

        case ENT_SPIDER:
            /* Black body */
            SetAPen(rp, COL_BLACK);
            RectFill(rp, sx + 4, sy + 4, sx + 11, sy + 11);
            /* Legs extending outward */
            if (e->frame) {
                Move(rp, sx + 4, sy + 5); Draw(rp, sx, sy + 2);
                Move(rp, sx + 11, sy + 5); Draw(rp, sx + 15, sy + 2);
                Move(rp, sx + 4, sy + 9); Draw(rp, sx, sy + 13);
                Move(rp, sx + 11, sy + 9); Draw(rp, sx + 15, sy + 13);
            } else {
                Move(rp, sx + 4, sy + 5); Draw(rp, sx + 1, sy + 1);
                Move(rp, sx + 11, sy + 5); Draw(rp, sx + 14, sy + 1);
                Move(rp, sx + 4, sy + 9); Draw(rp, sx + 1, sy + 14);
                Move(rp, sx + 11, sy + 9); Draw(rp, sx + 14, sy + 14);
            }
            /* Thread from top */
            SetAPen(rp, COL_WHITE);
            Move(rp, sx + 7, sy); Draw(rp, sx + 7, sy + 4);
            /* Eyes */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 5, sy + 6, sx + 6, sy + 7);
            RectFill(rp, sx + 9, sy + 6, sx + 10, sy + 7);
            break;

        case ENT_ANT:
            /* Small red body */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 5, sy + 6, sx + 10, sy + 12);
            /* Head */
            RectFill(rp, sx + 6, sy + 3, sx + 9, sy + 6);
            /* Antennae */
            SetAPen(rp, COL_BLACK);
            Move(rp, sx + 6, sy + 3); Draw(rp, sx + 4, sy);
            Move(rp, sx + 9, sy + 3); Draw(rp, sx + 11, sy);
            /* Legs */
            if (e->frame) {
                Move(rp, sx + 5, sy + 8); Draw(rp, sx + 2, sy + 14);
                Move(rp, sx + 10, sy + 8); Draw(rp, sx + 13, sy + 14);
            } else {
                Move(rp, sx + 5, sy + 8); Draw(rp, sx + 3, sy + 14);
                Move(rp, sx + 10, sy + 8); Draw(rp, sx + 12, sy + 14);
            }
            break;
        }
    }
}

/* ---- items.c */
void items_draw(struct RastPort *rp, int cam_x) {
    int i, sx, sy;
    Item *it;

    for (i = 0; i < MAX_ITEMS; i++) {
        it = &game.items[i];
        if (!it->active) continue;

        sx = it->x - cam_x;
        sy = it->y + it->float_off;

        if (sx < 0 || sx + 16 > SCREEN_W) continue;
        if (sy < 0 || sy + 16 > HUD_Y) continue;

        switch (it->type) {
        case ENT_BOARD:
            /* Computer circuit board - green with gold traces */
            SetAPen(rp, COL_DKGREEN);
            RectFill(rp, sx + 2, sy + 3, sx + 13, sy + 12);
            SetAPen(rp, COL_YELLOW);
            Move(rp, sx + 4, sy + 5); Draw(rp, sx + 11, sy + 5);
            Move(rp, sx + 4, sy + 8); Draw(rp, sx + 11, sy + 8);
            Move(rp, sx + 7, sy + 3); Draw(rp, sx + 7, sy + 12);
            /* Chips */
            SetAPen(rp, COL_BLACK);
            RectFill(rp, sx + 4, sy + 6, sx + 6, sy + 7);
            RectFill(rp, sx + 9, sy + 9, sx + 11, sy + 10);
            break;

        case ENT_DISKETTE:
            /* 3.5" floppy disk */
            SetAPen(rp, COL_BLUE);
            RectFill(rp, sx + 2, sy + 2, sx + 13, sy + 13);
            /* Metal slider */
            SetAPen(rp, COL_GREY);
            RectFill(rp, sx + 4, sy + 2, sx + 11, sy + 4);
            /* Label */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 4, sy + 7, sx + 11, sy + 11);
            /* Hub hole */
            SetAPen(rp, COL_BLACK);
            RectFill(rp, sx + 7, sy + 9, sx + 8, sy + 10);
            break;

        case ENT_BALL:
            /* Colorful ball */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 4, sy + 3, sx + 11, sy + 12);
            RectFill(rp, sx + 3, sy + 5, sx + 12, sy + 10);
            /* Highlight */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 5, sy + 4, sx + 7, sy + 6);
            /* Stripe */
            SetAPen(rp, COL_YELLOW);
            Move(rp, sx + 3, sy + 7); Draw(rp, sx + 12, sy + 7);
            break;

        case ENT_PILLOW_C:
            /* Pillow (collectible) */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 2, sy + 4, sx + 13, sy + 12);
            /* Puffy corners */
            RectFill(rp, sx + 3, sy + 3, sx + 12, sy + 13);
            /* Stitch lines */
            SetAPen(rp, COL_GREY);
            Move(rp, sx + 2, sy + 8); Draw(rp, sx + 13, sy + 8);
            Move(rp, sx + 7, sy + 4); Draw(rp, sx + 7, sy + 12);
            break;

        case ENT_PLANT:
            /* Green potted plant */
            SetAPen(rp, COL_LTGREEN);
            RectFill(rp, sx + 4, sy + 2, sx + 11, sy + 7);
            RectFill(rp, sx + 2, sy + 4, sx + 13, sy + 6);
            /* Stem */
            SetAPen(rp, COL_GREEN);
            RectFill(rp, sx + 7, sy + 7, sx + 8, sy + 10);
            /* Pot */
            SetAPen(rp, COL_ORANGE);
            RectFill(rp, sx + 4, sy + 10, sx + 11, sy + 14);
            SetAPen(rp, COL_DKORANGE);
            Move(rp, sx + 4, sy + 10); Draw(rp, sx + 11, sy + 10);
            break;

        case ENT_BRIEFCASE:
            /* Briefcase power-up */
            SetAPen(rp, COL_BROWN);
            RectFill(rp, sx + 1, sy + 4, sx + 14, sy + 13);
            /* Handle */
            SetAPen(rp, COL_DKBROWN);
            RectFill(rp, sx + 5, sy + 2, sx + 10, sy + 4);
            /* Clasp */
            SetAPen(rp, COL_YELLOW);
            RectFill(rp, sx + 7, sy + 8, sx + 8, sy + 9);
            /* Shine */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 2, sy + 5, sx + 4, sy + 6);
            break;

        case ENT_HEART:
            /* Heart shape */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 3, sy + 4, sx + 7, sy + 8);
            RectFill(rp, sx + 8, sy + 4, sx + 12, sy + 8);
            RectFill(rp, sx + 2, sy + 5, sx + 13, sy + 7);
            RectFill(rp, sx + 4, sy + 8, sx + 11, sy + 11);
            RectFill(rp, sx + 5, sy + 11, sx + 10, sy + 12);
            RectFill(rp, sx + 6, sy + 12, sx + 9, sy + 13);
            /* Highlight */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 4, sy + 5, sx + 5, sy + 6);
            break;

        case ENT_PILLOW_P:
            /* Pillow power-up (extra life) - with "ZZ" */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 2, sy + 4, sx + 13, sy + 12);
            RectFill(rp, sx + 3, sy + 3, sx + 12, sy + 13);
            SetAPen(rp, COL_BLUE);
            /* ZZ pattern */
            Move(rp, sx + 5, sy + 6); Draw(rp, sx + 8, sy + 6);
            Draw(rp, sx + 5, sy + 10); Draw(rp, sx + 8, sy + 10);
            break;

        case ENT_BEER:
            /* Beer glass */
            SetAPen(rp, COL_YELLOW);
            RectFill(rp, sx + 4, sy + 4, sx + 11, sy + 13);
            /* Foam */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 3, sy + 2, sx + 12, sy + 5);
            /* Glass outline */
            SetAPen(rp, COL_GREY);
            Move(rp, sx + 4, sy + 4); Draw(rp, sx + 4, sy + 13);
            Move(rp, sx + 11, sy + 4); Draw(rp, sx + 11, sy + 13);
            /* Handle */
            SetAPen(rp, COL_GREY);
            RectFill(rp, sx + 12, sy + 6, sx + 14, sy + 10);
            break;

        case ENT_WINE:
            /* Wine glass */
            SetAPen(rp, COL_GREY);
            /* Stem */
            RectFill(rp, sx + 7, sy + 9, sx + 8, sy + 12);
            /* Base */
            RectFill(rp, sx + 5, sy + 12, sx + 10, sy + 13);
            /* Bowl */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 4, sy + 3, sx + 11, sy + 9);
            RectFill(rp, sx + 5, sy + 2, sx + 10, sy + 3);
            /* Highlight */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 5, sy + 4, sx + 6, sy + 5);
            break;
        }
    }
}

/* ---- hud.c */
static void draw_heart(struct RastPort *rp, int x, int y, int filled) {
    int color = filled ? COL_RED : COL_DKBROWN;
    SetAPen(rp, color);
    RectFill(rp, x, y + 1, x + 2, y + 4);
    RectFill(rp, x + 3, y + 1, x + 5, y + 4);
    RectFill(rp, x, y + 2, x + 5, y + 3);
    RectFill(rp, x + 1, y + 4, x + 4, y + 5);
    RectFill(rp, x + 2, y + 5, x + 3, y + 6);
}

/* ---- title.c */
static void draw_character_preview(struct RastPort *rp, int x, int y, int character, int selected) {
    int bw, ph;

    /* Selection box */
    if (selected) {
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, x - 2, y - 2, x + 30, y + 42);
    }

    /* Background */
    SetAPen(rp, COL_SKY2);
    RectFill(rp, x, y, x + 28, y + 40);

    if (character == CHAR_RJ) {
        bw = 10; ph = 28;
        /* RJ */
        SetAPen(rp, COL_GREY);
        RectFill(rp, x + 10, y + 4, x + 19, y + 6);
        SetAPen(rp, COL_SKIN);
        RectFill(rp, x + 10, y + 7, x + 19, y + 12);
        SetAPen(rp, COL_BLACK);
        RectFill(rp, x + 11, y + 12, x + 18, y + 13);
        RectFill(rp, x + 15, y + 8, x + 16, y + 9);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, x + 9, y + 14, x + 20, y + 22);
        SetAPen(rp, COL_BLUE);
        RectFill(rp, x + 9, y + 23, x + 20, y + 30);
        SetAPen(rp, COL_BROWN);
        RectFill(rp, x + 9, y + 31, x + 20, y + 34);
    } else {
        bw = 14; ph = 24;
        /* Dale */
        SetAPen(rp, COL_GREY);
        RectFill(rp, x + 8, y + 6, x + 21, y + 8);
        SetAPen(rp, COL_SKIN);
        RectFill(rp, x + 8, y + 9, x + 21, y + 14);
        SetAPen(rp, COL_BLACK);
        RectFill(rp, x + 9, y + 14, x + 20, y + 15);
        RectFill(rp, x + 15, y + 10, x + 16, y + 11);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, x + 7, y + 16, x + 22, y + 22);
        SetAPen(rp, COL_BLUE);
        RectFill(rp, x + 8, y + 23, x + 21, y + 28);
        SetAPen(rp, COL_BROWN);
        RectFill(rp, x + 8, y + 29, x + 21, y + 32);
    }

    (void)bw;
    (void)ph;
}
