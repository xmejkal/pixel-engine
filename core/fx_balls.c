// The balls' solver (P598): fx_balls.h says what each function does. --- MVP (P598) ---
#include "fx_balls.h"
#include "grid.h"

#include <string.h>

// The blocks after the struct, found from cap at each call: the grid first, so the floats stay aligned.
#define GRID(b) ((grid_t *)((b) + 1))
#define X(b) ((float *)((uint8_t *)GRID(b) + grid_bytes(FX_BALLS_COLS, FX_BALLS_ROWS, (b)->cap)))
#define Y(b) (X(b) + (b)->cap)
#define PX(b) (Y(b) + (b)->cap)
#define PY(b) (PX(b) + (b)->cap)
#define VX(b) (PY(b) + (b)->cap)
#define VY(b) (VX(b) + (b)->cap)
#define R(b) ((uint8_t *)(VY(b) + (b)->cap))
#define COLOUR(b) (R(b) + (b)->cap)

// The arrays a substep works on, found once a substep: each macro above calls grid_bytes in grid.c, which the compiler
// cannot inline, and a call per array read made a substep 2.85 ms for 300 balls on the board (run97).
typedef struct {
    float *x, *y, *px, *py, *vx, *vy;
    const uint8_t *r;
} view_t;

static view_t view_of(fx_balls_t *b) {
    view_t v = {X(b), Y(b), PX(b), PY(b), VX(b), VY(b), R(b)};
    return v;
}

size_t fx_balls_bytes(int32_t cap) {
    return sizeof(fx_balls_t) + grid_bytes(FX_BALLS_COLS, FX_BALLS_ROWS, cap)
        + (6 * sizeof(float) + 2 * sizeof(uint8_t)) * (size_t)cap;
}

void fx_balls_init(fx_balls_t *b, int32_t cap) {
    memset(b, 0, fx_balls_bytes(cap));
    b->cap = cap;
    grid_init(GRID(b), FX_BALLS_COLS, FX_BALLS_ROWS, FX_BALLS_CELL, cap);
    for (int32_t r = FX_BALLS_R_MIN; r <= FX_BALLS_R_MAX; r++) {  // the disc of radius r + 1/2, in whole px, no sqrt
        for (int32_t dx = 0; dx <= r; dx++) {
            int32_t h = r;
            while (h * h + dx * dx > r * r + r) {
                h--;
            }
            b->span[r][dx] = (uint8_t)h;
        }
    }
}

int32_t fx_balls_spawn(fx_balls_t *b, float x, float y, float vx, float vy, int32_t r, int32_t colour) {
    if (b->n == b->cap) {
        return -1;
    }
    int32_t i = b->n++;
    X(b)[i] = PX(b)[i] = x;
    Y(b)[i] = PY(b)[i] = y;
    VX(b)[i] = vx;
    VY(b)[i] = vy;
    R(b)[i] = (uint8_t)r;
    COLOUR(b)[i] = (uint8_t)colour;
    return i;
}

void fx_balls_clear(fx_balls_t *b) {
    b->n = 0;
}

void fx_balls_set_material(fx_balls_t *b, float friction, float restitution, float restitution_threshold) {
    b->friction = friction;
    b->restitution = restitution;
    b->restitution_threshold = restitution_threshold;
}

void fx_balls_kick(fx_balls_t *b, float vx, float vy) {
    float *bvx = VX(b), *bvy = VY(b);
    for (int32_t i = 0; i < b->n; i++) {
        bvx[i] += vx;
        bvy[i] += vy;
    }
}

// 1 / sqrt(x) with no division and no sqrtf, both library calls on the S3: a full pile paid one sqrtf and four
// __divsf3 per touching pair in each pass (run99: 100 resting balls, 23 ms a frame). The method is the fast inverse
// square root (Lomont, "Fast Inverse Square Root", 2003, his constant 0x5f375a86), with two Newton steps, so its
// relative error is under 1e-5. Written from the formula, no code copied (D14).
static float inv_sqrt(float x) {
    union {
        float f;
        uint32_t u;
    } c = {x};
    c.u = 0x5f375a86u - (c.u >> 1);
    float y = c.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return y * (1.5f - 0.5f * x * y * y);
}

// The part of a push that ball i takes from a pair with ball j, by their radii: rj^2 / (ri^2 + rj^2), the lighter
// ball moves further. Constants, so a contact divides nothing.
#define SHARE(a, b) ((float)((b) * (b)) / (float)((a) * (a) + (b) * (b)))
#define SHARE_ROW(a) {SHARE(a, 2), SHARE(a, 3), SHARE(a, 4), SHARE(a, 5)}
static const float share[4][4] = {SHARE_ROW(2), SHARE_ROW(3), SHARE_ROW(4), SHARE_ROW(5)};  // [ri - 2][rj - 2]

// A bounce for the substep's end, or one more lost.
static void bounce_add(fx_balls_t *b, int32_t i, int32_t j, float speed) {
    if (b->bounces_n == FX_BALLS_BOUNCES_MAX) {
        b->bounces_dropped++;
        return;
    }
    fx_bounce_t *e = &b->bounces[b->bounces_n++];
    e->i = i;
    e->j = j;
    e->speed = speed;
}

// The sliding part t of a contact's move, less what friction holds: all of it while |t| is up to limit (static), else
// limit of it (kinetic). Position-based Coulomb friction as Macklin et al. give it ("Unified Particle Physics for
// Real-Time Applications", SIGGRAPH 2014, section 6.1), written from the paper, no code copied (D14). Return the part
// of t to take back: t itself or t scaled down.
static float held(float t2, float limit) {
    return t2 <= limit * limit ? 1.0f : limit * inv_sqrt(t2);
}

// Push balls i and j apart when they overlap; friction takes back their sliding; in the first pass, a hit when they
// close at hit_min or more, and a bounce when they close at the restitution threshold or more.
static void contact(fx_balls_t *b, const view_t *v, int32_t i, int32_t j, int32_t pass, float hit_min) {
    float *x = v->x, *y = v->y;
    const uint8_t *r = v->r;
    float dx = x[j] - x[i], dy = y[j] - y[i], rr = (float)(r[i] + r[j]);
    float d2 = dx * dx + dy * dy;
    if (d2 >= rr * rr) {
        return;
    }
    float d = 0.0f, nx = 0.0f, ny = -1.0f;  // the normal from i to j; (0, -1) for two centres in one place
    if (d2 >= 1e-6f) {  // d >= 1e-3 px
        float inv = inv_sqrt(d2);
        d = d2 * inv;
        nx = dx * inv;
        ny = dy * inv;
    }
    float push = rr - d;
    float si = share[r[i] - FX_BALLS_R_MIN][r[j] - FX_BALLS_R_MIN], wi = push * si, wj = push - wi;
    x[i] -= nx * wi;
    y[i] -= ny * wi;
    x[j] += nx * wj;
    y[j] += ny * wj;
    if (b->friction > 0.0f) {  // the pair's relative move this substep, its sliding part t, split as the push is
        float mx = (x[i] - v->px[i]) - (x[j] - v->px[j]), my = (y[i] - v->py[i]) - (y[j] - v->py[j]);
        float mn = mx * nx + my * ny, tx = mx - mn * nx, ty = my - mn * ny, t2 = tx * tx + ty * ty;
        if (t2 > 0.0f) {
            float k = held(t2, b->friction * push), sj = 1.0f - si;
            x[i] -= tx * k * si;
            y[i] -= ty * k * si;
            x[j] += tx * k * sj;
            y[j] += ty * k * sj;
        }
    }
    if (pass == 0) {
        const float *vx = v->vx, *vy = v->vy;
        float closing = (vx[i] - vx[j]) * nx + (vy[i] - vy[j]) * ny;
        if (closing >= hit_min) {
            events_add(&b->events, i, EVENT_HIT, (int32_t)closing);
        }
        if (b->restitution > 0.0f && closing >= b->restitution_threshold && closing > 0.0f) {
            bounce_add(b, i, j, closing);
        }
    }
}

// A wall's friction on ball i: its move along the wall (*q from *pq) loses up to friction times the depth the wall
// clamped, the one-dimensional case of held().
static void wall_friction(const fx_balls_t *b, float *q, const float *pq, float depth) {
    float t = *q - *pq, limit = b->friction * depth;
    *q -= t > limit ? limit : (t < -limit ? -limit : t);
}

// In the first pass, a bounce off wall j when ball i ran into it at the restitution threshold or more.
static void wall_bounce(fx_balls_t *b, int32_t i, int32_t j, float speed, int32_t pass) {
    if (pass == 0 && b->restitution > 0.0f && speed >= b->restitution_threshold && speed > 0.0f) {
        bounce_add(b, i, j, speed);
    }
}

// Ball i back inside the walls, its move into a wall gone (px or py onto it, so the next velocity has none of it:
// else a landing fired a second hit from the move that the floor cut) and its move along it held by friction; in the
// first pass, a hit when it ran into one at hit_min or more, and a bounce at the restitution threshold or more.
static void walls(fx_balls_t *b, const view_t *v, int32_t i, int32_t pass, float hit_min) {
    float *x = v->x, *y = v->y, r = (float)v->r[i];
    float speed = -1.0f;  // under any hit_min while no wall is touched
    if (x[i] < r) {
        float depth = r - x[i];
        x[i] = v->px[i] = r;
        speed = -v->vx[i];
        wall_friction(b, &y[i], &v->py[i], depth);
        wall_bounce(b, i, FX_BALLS_WALL_LEFT, speed, pass);
    } else if (x[i] > FX_BALLS_W - r) {
        float depth = x[i] - (FX_BALLS_W - r);
        x[i] = v->px[i] = FX_BALLS_W - r;
        speed = v->vx[i];
        wall_friction(b, &y[i], &v->py[i], depth);
        wall_bounce(b, i, FX_BALLS_WALL_RIGHT, speed, pass);
    }
    if (y[i] > FX_BALLS_H - r) {
        float depth = y[i] - (FX_BALLS_H - r);
        y[i] = v->py[i] = FX_BALLS_H - r;
        wall_friction(b, &x[i], &v->px[i], depth);
        wall_bounce(b, i, FX_BALLS_WALL_FLOOR, v->vy[i], pass);
        speed = v->vy[i] > speed ? v->vy[i] : speed;
    }
    if (pass == 0 && speed >= hit_min && speed >= 0.0f) {
        events_add(&b->events, i, EVENT_HIT, (int32_t)speed);
    }
}

// The substep's bounces, once its velocities are set: a pair's relative velocity along its normal now (or a wall
// ball's into its wall) becomes at least restitution times its closing speed before the solve, apart. The change is
// split as a push is, the lighter ball more. Restitution at the velocity level, after the position solve, as Macklin
// et al. 2014 (section 6) and Müller et al., "Detailed Rigid Body Simulation with Extended Position Based Dynamics"
// (2020, the velocity solve), give it; written from the papers, no code copied (D14).
static void bounce(fx_balls_t *b, const view_t *v) {
    float *x = v->x, *y = v->y, *vx = v->vx, *vy = v->vy, e = b->restitution;
    for (int32_t k = 0; k < b->bounces_n; k++) {
        int32_t i = b->bounces[k].i, j = b->bounces[k].j;
        float apart = e * b->bounces[k].speed;
        if (j == FX_BALLS_WALL_LEFT) {
            vx[i] = vx[i] < apart ? apart : vx[i];
        } else if (j == FX_BALLS_WALL_RIGHT) {
            vx[i] = vx[i] > -apart ? -apart : vx[i];
        } else if (j == FX_BALLS_WALL_FLOOR) {
            vy[i] = vy[i] > -apart ? -apart : vy[i];
        } else {
            float dx = x[j] - x[i], dy = y[j] - y[i], d2 = dx * dx + dy * dy, nx = 0.0f, ny = -1.0f;
            if (d2 >= 1e-6f) {
                float inv = inv_sqrt(d2);
                nx = dx * inv;
                ny = dy * inv;
            }
            float more = (vx[i] - vx[j]) * nx + (vy[i] - vy[j]) * ny + apart;  // > 0: they part slower than apart
            if (more > 0.0f) {
                float si = share[v->r[i] - FX_BALLS_R_MIN][v->r[j] - FX_BALLS_R_MIN], sj = 1.0f - si;
                vx[i] -= nx * more * si;
                vy[i] -= ny * more * si;
                vx[j] += nx * more * sj;
                vy[j] += ny * more * sj;
            }
        }
    }
}

static void substep(fx_balls_t *b, float dt, float gravity, float damping, float hit_min) {
    int32_t n = b->n;
    view_t v = view_of(b);
    float *x = v.x, *y = v.y, *px = v.px, *py = v.py, *vx = v.vx, *vy = v.vy;
    grid_t *g = GRID(b);
    b->bounces_n = 0;
    for (int32_t i = 0; i < n; i++) {
        vy[i] += gravity * dt;
        px[i] = x[i];
        py[i] = y[i];
        x[i] += vx[i] * dt;
        y[i] += vy[i] * dt;
    }
    grid_build(g, x, y, n);
    for (int32_t pass = 0; pass < FX_BALLS_PASSES; pass++) {
        for (int32_t i = 0; i < n; i++) {
            int32_t cx, cy;
            grid_locate(g, x[i], y[i], &cx, &cy);
            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = -1; dx <= 1; dx++) {
                    const int32_t *items;
                    int32_t m = grid_cell(g, cx + dx, cy + dy, &items);
                    for (int32_t k = 0; k < m; k++) {
                        if (items[k] > i) {  // each pair once, from its lower index
                            contact(b, &v, i, items[k], pass, hit_min);
                        }
                    }
                }
            }
            walls(b, &v, i, pass, hit_min);
        }
    }
    float inv_dt = damping / dt;
    for (int32_t i = 0; i < n; i++) {
        float mx = x[i] - px[i], my = y[i] - py[i], m2 = mx * mx + my * my;
        if (m2 > FX_BALLS_MOVE_MAX * FX_BALLS_MOVE_MAX) {
            float s = FX_BALLS_MOVE_MAX * inv_sqrt(m2);
            mx *= s;
            my *= s;
            x[i] = px[i] + mx;
            y[i] = py[i] + my;
        }
        vx[i] = mx * inv_dt;
        vy[i] = my * inv_dt;
    }
    bounce(b, &v);
}

void fx_balls_step(fx_balls_t *b, int32_t steps, int32_t substeps, float dt, float gravity, float damping,
                   float hit_min) {
    events_clear(&b->events);
    b->bounces_dropped = 0;
    float sub_dt = dt / (float)substeps, keep = 1.0f - (1.0f - damping) / (float)substeps;  // the step's loss, spread
    for (int32_t k = 0; k < steps * substeps; k++) {
        substep(b, sub_dt, gravity, keep, hit_min);
    }
}

int32_t fx_balls_read(const fx_balls_t *b, int16_t *out) {
    const float *x = X(b), *y = Y(b);
    const uint8_t *r = R(b);
    for (int32_t i = 0; i < b->n; i++) {
        out[3 * i] = (int16_t)x[i];
        out[3 * i + 1] = (int16_t)y[i];
        out[3 * i + 2] = r[i];
    }
    return b->n;
}

int32_t fx_balls_drain(const fx_balls_t *b, int32_t *out, int32_t room) {
    return events_drain(&b->events, out, room);
}

void fx_balls_draw(const fx_balls_t *b, uint16_t *panel, const uint16_t *palette, uint16_t bg) {
    const float *x = X(b), *y = Y(b);
    const uint8_t *r = R(b), *colour = COLOUR(b);
    for (int32_t k = 0; k < FX_BALLS_W * FX_BALLS_H; k++) {
        panel[k] = bg;
    }
    for (int32_t i = 0; i < b->n; i++) {
        int32_t cx = (int32_t)x[i], cy = (int32_t)y[i], ri = r[i];
        uint16_t c = palette[colour[i]];
        for (int32_t dx = -ri; dx <= ri; dx++) {  // game column cx + dx is one panel row: its run in y is contiguous
            int32_t gx = cx + dx, h = b->span[ri][dx < 0 ? -dx : dx];
            if (gx < 0 || gx >= FX_BALLS_W) {
                continue;
            }
            int32_t y0 = cy - h < 0 ? 0 : cy - h, y1 = cy + h >= FX_BALLS_H ? FX_BALLS_H - 1 : cy + h;
            uint16_t *row = panel + (FX_BALLS_W - 1 - gx) * FX_BALLS_H;
            for (int32_t gy = y0; gy <= y1; gy++) {
                row[gy] = c;
            }
        }
    }
}
