// The balls' solver (P598): fx_balls.h says what each function does. --- MVP (P598) ---
#include "fx_balls.h"
#include "grid.h"

#include <math.h>
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

// Push balls i and j apart when they overlap; in the first pass, a hit when they close at hit_min or more.
static void contact(fx_balls_t *b, const view_t *v, int32_t i, int32_t j, int32_t pass, float hit_min) {
    float *x = v->x, *y = v->y;
    const uint8_t *r = v->r;
    float dx = x[j] - x[i], dy = y[j] - y[i], rr = (float)(r[i] + r[j]);
    float d2 = dx * dx + dy * dy;
    if (d2 >= rr * rr) {
        return;
    }
    float d = sqrtf(d2), nx = 0.0f, ny = -1.0f;  // the normal from i to j; (0, -1) for two centres in one place
    if (d >= 1e-3f) {
        nx = dx / d;
        ny = dy / d;
    }
    float mi = (float)(r[i] * r[i]), mj = (float)(r[j] * r[j]), push = rr - d;
    float wi = push * mj / (mi + mj), wj = push * mi / (mi + mj);  // the lighter ball moves further
    x[i] -= nx * wi;
    y[i] -= ny * wi;
    x[j] += nx * wj;
    y[j] += ny * wj;
    if (pass == 0) {
        const float *vx = v->vx, *vy = v->vy;
        float closing = (vx[i] - vx[j]) * nx + (vy[i] - vy[j]) * ny;
        if (closing >= hit_min) {
            events_add(&b->events, i, EVENT_HIT, (int32_t)closing);
        }
    }
}

// Ball i back inside the walls, its move into a wall gone (px or py onto it, so the next velocity has none of it:
// else a landing fired a second hit from the move that the floor cut); in the first pass, a hit when it ran into one
// at hit_min or more.
static void walls(fx_balls_t *b, const view_t *v, int32_t i, int32_t pass, float hit_min) {
    float *x = v->x, *y = v->y, r = (float)v->r[i];
    float speed = -1.0f;  // under any hit_min while no wall is touched
    if (x[i] < r) {
        x[i] = v->px[i] = r;
        speed = -v->vx[i];
    } else if (x[i] > FX_BALLS_W - r) {
        x[i] = v->px[i] = FX_BALLS_W - r;
        speed = v->vx[i];
    }
    if (y[i] > FX_BALLS_H - r) {
        y[i] = v->py[i] = FX_BALLS_H - r;
        speed = v->vy[i] > speed ? v->vy[i] : speed;
    }
    if (pass == 0 && speed >= hit_min && speed >= 0.0f) {
        events_add(&b->events, i, EVENT_HIT, (int32_t)speed);
    }
}

static void substep(fx_balls_t *b, float dt, float gravity, float damping, float hit_min) {
    int32_t n = b->n;
    view_t v = view_of(b);
    float *x = v.x, *y = v.y, *px = v.px, *py = v.py, *vx = v.vx, *vy = v.vy;
    grid_t *g = GRID(b);
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
            float s = FX_BALLS_MOVE_MAX / sqrtf(m2);
            mx *= s;
            my *= s;
            x[i] = px[i] + mx;
            y[i] = py[i] + my;
        }
        vx[i] = mx * inv_dt;
        vy[i] = my * inv_dt;
    }
}

void fx_balls_step(fx_balls_t *b, int32_t steps, int32_t substeps, float dt, float gravity, float damping,
                   float hit_min) {
    events_clear(&b->events);
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
