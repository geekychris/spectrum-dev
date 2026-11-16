/*
 * render.c - Ace Pilot's view on the Next: the original's render.c (ground
 * grid, horizon, wireframe models, bullets, particles, crosshair) drawn on
 * the 256 x 192 Layer 2 back buffer with sdk l2line.
 *
 * port: the original draws a 320 x 256 view with FOV 200 around (160, 128);
 * here it is 256 x 192, FOV 160 around (128, 96) (x 0.8: the same picture,
 * 6% less at the top and bottom). Models: one 3 x 3 matrix per object (the
 * camera's yaw and pitch and the object's yaw, Q9), its vertices transformed
 * relative to its position; the original rotates and transforms each vertex
 * in 16.16. Same lines, cheaper sums.
 */
#include <stdint.h>
#include "game.h"
#include "render.h"
#include "l2line.h"

#define FOV 160                     /* port: the original's FOV_SCALE 200 x 0.8 */

/* ---- colours: the original's 16 pens (main.c's palette, 12-bit RGB) in
   Layer 2's default RGB332 palette; port: depth shades from the 256 colours */
static const uint8_t pen[16] = {
    0x00, 0xFF, 0x1C, 0x1F, 0x14, 0x0C, 0xB0, 0x92,
    0xE0, 0xF0, 0xFC, 0x80, 0x0B, 0x08, 0xF4, 0xFD,
};

/* ---- the camera of the view being drawn */
static int32_t cam_x, cam_y, cam_z;
static uint8_t cam_yaw, cam_pitch;
static int16_t hcx, hcy;            /* the view's centre */

static int16_t v_left, v_top, v_right, v_bottom;

void render_view(int16_t left, int16_t top, int16_t right, int16_t bottom)
{
    v_left = left;
    v_top = top;
    v_right = right;
    v_bottom = bottom;
    hcx = (left + right + 1) / 2;
    hcy = (top + bottom + 1) / 2;
}

static void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t colour)
{
    l2l_line(x0, y0, x1, y1, colour);
}

/* the original's transform_point (16.16 trig, 32-bit sums) */
static void xform(int32_t wx, int32_t wy, int32_t wz, int32_t *cx, int32_t *cy, int32_t *cz)
{
    int32_t dx = wx - cam_x, dy = wy - cam_y, dz = wz - cam_z;
    int32_t cyaw = cos_tab[cam_yaw] >> 8, syaw = sin_tab[cam_yaw] >> 8;
    int32_t cp = cos_tab[cam_pitch] >> 8, sp = sin_tab[cam_pitch] >> 8;
    int32_t rx = (dx * cyaw + dz * syaw) >> 8;
    int32_t rz = (-dx * syaw + dz * cyaw) >> 8;
    *cx = rx;
    *cy = (dy * cp + rz * sp) >> 8;
    *cz = (-dy * sp + rz * cp) >> 8;
}

/* screen point of a camera-space point in front of the near plane */
static uint8_t project(int32_t cx, int32_t cy, int32_t cz, int16_t *sx, int16_t *sy)
{
    int32_t px, py;
    if (cz < NEAR_CLIP || cz > FAR_CLIP) return 0;
    px = hcx + cx * FOV / cz;
    py = hcy - cy * FOV / cz;
    if (px < -400 || px > 656 || py < -400 || py > 592) return 0;   /* the original's safe range x 0.8 */
    *sx = (int16_t)px;
    *sy = (int16_t)py;
    return 1;
}

/* ---- models */

static int16_t vx_s[24], vy_s[24];
static uint8_t vis[24];

static void render_model(const Model *m, int32_t wx, int32_t wy, int32_t wz, uint8_t yaw, uint8_t obj_type)
{
    int32_t ox, oy, oz;
    int16_t m00, m02, m10, m11, m12, m20, m21, m22;
    uint8_t a = (uint8_t)(cam_yaw - yaw), i;
    int16_t c = (int16_t)(cos_tab[a] >> 7), s = (int16_t)(sin_tab[a] >> 7);       /* Q9 */
    int16_t cp = (int16_t)(cos_tab[cam_pitch] >> 7), sp = (int16_t)(sin_tab[cam_pitch] >> 7);
    uint8_t colour;
    int32_t depth;

    xform(wx, wy, wz, &ox, &oy, &oz);
    /* the original culls on vertex 0's depth (close enough: the centre's, +-20) */
    m00 = c;   m02 = s;
    m10 = (int16_t)(-(int32_t)sp * s >> 9);  m11 = cp;  m12 = (int16_t)((int32_t)sp * c >> 9);
    m20 = (int16_t)(-(int32_t)cp * s >> 9);  m21 = -sp; m22 = (int16_t)((int32_t)cp * c >> 9);
    for (i = 0; i < m->num_verts; i++) {
        int16_t x = m->verts[i][0], y = m->verts[i][1], z = m->verts[i][2];
        int32_t cx = ox + ((m00 * x + m02 * z) >> 9);
        int32_t cy = oy + ((m10 * x + m11 * y + m12 * z) >> 9);
        int32_t cz = oz + ((m20 * x + m21 * y + m22 * z) >> 9);
        if (i == 0) depth = cz;
        vis[i] = project(cx, cy, cz, &vx_s[i], &vy_s[i]);
    }
    if (depth < NEAR_CLIP || depth > FAR_CLIP) return;
    colour = pen[get_obj_color(obj_type, depth)];
    for (i = 0; i < m->num_edges; i++) {
        uint8_t v1 = (uint8_t)m->edges[i][0], v2 = (uint8_t)m->edges[i][1];
        if (vis[v1] && vis[v2]) line(vx_s[v1], vy_s[v1], vx_s[v2], vy_s[v2], colour);
    }
}

/* ---- ground: the original's camera-relative grid and analytic horizon */

#define GRID_SPACING 200
#define GRID_LINES   21
#define GROUND_FAR   8000

static int16_t clamp2000(int32_t v)
{
    if (v < -2000) return -2000;
    if (v > 2000) return 2000;
    return (int16_t)v;
}

static void render_ground(void)
{
    int32_t ground_dy = GROUND_Y - cam_y;
    int32_t cpitch = cos_tab[cam_pitch] >> 8, spitch = sin_tab[cam_pitch] >> 8;
    int32_t cyaw = cos_tab[cam_yaw] >> 8, syaw = sin_tab[cam_yaw] >> 8;
    uint8_t grid = pen[get_obj_color(OBJ_GRID, 500)];
    int16_t i;

    {   /* horizon: a ground point 20000 ahead */
        int32_t hcy_ = (ground_dy * cpitch >> 8) + (20000L * spitch >> 8);
        int32_t hcz = -(ground_dy * spitch >> 8) + (20000L * cpitch >> 8);
        int32_t hy;
        if (hcz < 1) hcz = 1;
        hy = hcy - hcy_ * FOV / hcz;
        if (hy >= v_top && hy <= v_bottom)
            line(v_left, (int16_t)hy, v_right, (int16_t)hy,
                 pen[g_tune.display_mode == DISPLAY_CLASSIC ? 2 : 13]);
    }
    {
        int32_t lat_offset = (cam_x * cyaw - cam_z * syaw) >> 8;
        int32_t fwd_offset = (cam_x * syaw + cam_z * cyaw) >> 8;
        int32_t lat_shift = lat_offset - lat_offset / GRID_SPACING * GRID_SPACING;
        int32_t fwd_shift = fwd_offset - fwd_offset / GRID_SPACING * GRID_SPACING;
        int32_t acz_near = (-ground_dy * spitch + (int32_t)NEAR_CLIP * cpitch) >> 8;
        int32_t acy_near = (ground_dy * cpitch + (int32_t)NEAR_CLIP * spitch) >> 8;
        int32_t acz_far = (-ground_dy * spitch + (int32_t)GROUND_FAR * cpitch) >> 8;
        int32_t acy_far = (ground_dy * cpitch + (int32_t)GROUND_FAR * spitch) >> 8;
        int16_t y0, y1;
        if (acz_near < 1) acz_near = 1;
        if (acz_far < 1) acz_far = acz_near;
        y0 = clamp2000(hcy - acy_near * FOV / acz_near);
        y1 = clamp2000(hcy - acy_far * FOV / acz_far);
        for (i = 0; i < GRID_LINES; i++) {          /* forward lines */
            int32_t cx = (int32_t)(i - GRID_LINES / 2) * GRID_SPACING - lat_shift;
            line(clamp2000(hcx + cx * FOV / acz_near), y0, clamp2000(hcx + cx * FOV / acz_far), y1, grid);
        }
        for (i = 1; i <= GRID_LINES; i++) {         /* sideways lines */
            int32_t cz = (int32_t)i * GRID_SPACING - fwd_shift;
            int32_t acz, acy;
            int16_t y;
            if (cz < 1) continue;
            acz = (-ground_dy * spitch + cz * cpitch) >> 8;
            acy = (ground_dy * cpitch + cz * spitch) >> 8;
            if (acz < 1) continue;
            y = clamp2000(hcy - acy * FOV / acz);
            line(clamp2000(hcx - (int32_t)(GRID_LINES / 2) * GRID_SPACING * FOV / acz), y,
                 clamp2000(hcx + (int32_t)(GRID_LINES / 2) * GRID_SPACING * FOV / acz), y, grid);
        }
    }
}

/* ---- bullets, particles, crosshair */

static void render_bullets(const GameWorld *w)
{
    uint8_t i;
    for (i = 0; i < MAX_BULLETS; i++) {
        const Bullet *b = &w->bullets[i];
        int32_t cx, cy, cz, tx, ty, tz;
        int16_t sx, sy, sx2, sy2;
        uint8_t colour;
        if (!b->active) continue;
        xform(b->pos.x, b->pos.y, b->pos.z, &cx, &cy, &cz);
        if (!project(cx, cy, cz, &sx, &sy)) continue;
        colour = pen[get_obj_color(b->owner == 2 ? OBJ_BULLET_E : OBJ_BULLET_P, cz)];
        xform(b->pos.x - b->vx, b->pos.y - b->vy, b->pos.z - b->vz, &tx, &ty, &tz);
        if (project(tx, ty, tz, &sx2, &sy2)) line(sx, sy, sx2, sy2, colour);
        else line(sx, sy, sx + 1, sy, colour);
    }
}

static void render_particles(const GameWorld *w)
{
    uint8_t i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &w->particles[i];
        int32_t cx, cy, cz;
        int16_t sx, sy;
        uint8_t colour;
        if (!p->active) continue;
        xform(p->pos.x, p->pos.y, p->pos.z, &cx, &cy, &cz);
        if (!project(cx, cy, cz, &sx, &sy)) continue;
        if (g_tune.display_mode == DISPLAY_CLASSIC) colour = pen[p->life > 10 ? 2 : 4];
        else colour = pen[p->life > 10 ? 9 : 14];
        line(sx, sy, sx, sy, colour);
    }
}

static void render_crosshair(void)
{
    uint8_t colour = pen[g_tune.display_mode == DISPLAY_CLASSIC ? 2 : 1];
    line(hcx - 10, hcy, hcx - 3, hcy, colour);      /* port: the original's 12 / 4 x 0.8 */
    line(hcx + 3, hcy, hcx + 10, hcy, colour);
    line(hcx, hcy - 10, hcx, hcy - 3, colour);
    line(hcx, hcy + 3, hcx, hcy + 10, colour);
    line(hcx, hcy, hcx, hcy, colour);
}

void render_scene(const GameWorld *w, uint8_t player)
{
    const Camera *c = &w->players[player].cam;
    uint8_t i;
    cam_x = c->pos.x;
    cam_y = c->pos.y;
    cam_z = c->pos.z;
    cam_yaw = (uint8_t)c->yaw;
    cam_pitch = (uint8_t)c->pitch;
    l2l_ytop = (uint8_t)v_top;
    l2l_ybot = (uint8_t)v_bottom;

    render_ground();
    for (i = 0; i < MAX_GROUND; i++) {
        const GroundTarget *g = &w->ground[i];
        if (!g->active) continue;
        render_model(&models[g->model_id], g->pos.x, g->pos.y, g->pos.z, (uint8_t)g->yaw, OBJ_GROUND);
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &w->enemies[i];
        if (!e->active) continue;
        render_model(&models[e->model_id], e->pos.x, e->pos.y, e->pos.z, (uint8_t)e->yaw,
                     e->model_id == MODEL_BLIMP ? OBJ_BLIMP : OBJ_ENEMY);
    }
    render_bullets(w);
    render_particles(w);
    render_crosshair();
}
