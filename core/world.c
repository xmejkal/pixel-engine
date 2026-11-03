// The world's step in two modes (P589 probe): world.h says what each function does.
// --- probe, throwaway (P589) ---
#include "world.h"

#include <string.h>

#define Q_ONE 65536  // 1.0 in Q16.16

static int32_t to_q(float f) {  // f * 2^16 is exact in float32; round half away from 0
    float q = f * (float)Q_ONE;
    return (int32_t)(q >= 0.0f ? q + 0.5f : q - 0.5f);
}

static int32_t q_mul(int32_t a, int32_t b) {  // int64 product; / truncates toward 0 for a negative speed too
    return (int32_t)(((int64_t)a * b) / Q_ONE);
}

static void load_state(world_state_t *s, int32_t n) {
    memset(s, 0, sizeof(*s));
    s->n = n < 0 ? 0 : (n > WORLD_MAX_BODIES ? WORLD_MAX_BODIES : n);
}

static void add_event(world_state_t *s, int32_t id, int32_t kind, int32_t speed) {
    if (s->n_events == WORLD_MAX_EVENTS) {
        s->dropped++;
        return;
    }
    world_event_t *e = &s->events[s->n_events++];
    e->id = id;
    e->kind = kind;
    e->speed = speed;
}

static void clear_events(world_state_t *s) {
    s->n_events = 0;
    s->dropped = 0;
}

void world_load_float(world_float_t *w, int32_t n, const float *x, const float *y, const float *vx, const float *vy,
    const float *rest_y) {
    load_state(&w->s, n);
    for (int32_t i = 0; i < w->s.n; i++) {
        w->x[i] = x[i];
        w->y[i] = y[i];
        w->vx[i] = vx[i];
        w->vy[i] = vy[i];
        w->rest_y[i] = rest_y[i];
    }
}

void world_load_fixed(world_fixed_t *w, int32_t n, const float *x, const float *y, const float *vx, const float *vy,
    const float *rest_y) {
    load_state(&w->s, n);
    for (int32_t i = 0; i < w->s.n; i++) {
        w->x[i] = to_q(x[i]);
        w->y[i] = to_q(y[i]);
        w->vx[i] = to_q(vx[i]);
        w->vy[i] = to_q(vy[i]);
        w->rest_y[i] = to_q(rest_y[i]);
    }
}

void world_step_float(world_float_t *w, int32_t steps, int32_t rate, float gravity, float restitution, float friction) {
    world_state_t *s = &w->s;
    float dt = 1.0f / (float)rate;
    float rest_speed = 1.5f * gravity * dt;  // fall_bodies' rest speed
    clear_events(s);
    for (int32_t k = 0; k < steps; k++) {
        for (int32_t i = 0; i < s->n; i++) {
            if (s->asleep[i]) {
                continue;
            }
            s->awake_steps++;
            w->vy[i] += gravity * dt;
            w->x[i] += w->vx[i] * dt;
            w->y[i] += w->vy[i] * dt;
            if (w->y[i] >= w->rest_y[i]) {  // through the plane in this step: back onto it
                w->y[i] = w->rest_y[i];
                float v = w->vy[i];
                if (v < rest_speed) {  // too slow to leave the plane again: it rests
                    w->vy[i] = 0.0f;
                    w->vx[i] = 0.0f;
                    s->asleep[i] = 1;
                    add_event(s, i, WORLD_EVENT_SLEEP, 0);
                } else {
                    add_event(s, i, WORLD_EVENT_HIT, (int32_t)v);
                    w->vy[i] = -v * restitution;
                    w->vx[i] *= friction;
                }
            }
        }
    }
}

void world_step_fixed(world_fixed_t *w, int32_t steps, int32_t rate, float gravity, float restitution, float friction) {
    world_state_t *s = &w->s;
    int32_t g = to_q(gravity);
    int32_t g_step = g / rate;  // px/s gained per step: exact for 2000 / 100
    int32_t rest_speed = (int32_t)(((int64_t)3 * g) / (2 * rate));  // 1.5 * gravity / rate
    int32_t rest_q = to_q(restitution), friction_q = to_q(friction);
    clear_events(s);
    for (int32_t k = 0; k < steps; k++) {
        for (int32_t i = 0; i < s->n; i++) {
            if (s->asleep[i]) {
                continue;
            }
            s->awake_steps++;
            w->vy[i] += g_step;
            w->x[i] += w->vx[i] / rate;
            w->y[i] += w->vy[i] / rate;
            if (w->y[i] >= w->rest_y[i]) {
                w->y[i] = w->rest_y[i];
                int32_t v = w->vy[i];
                if (v < rest_speed) {
                    w->vy[i] = 0;
                    w->vx[i] = 0;
                    s->asleep[i] = 1;
                    add_event(s, i, WORLD_EVENT_SLEEP, 0);
                } else {
                    add_event(s, i, WORLD_EVENT_HIT, v / Q_ONE);
                    w->vy[i] = -q_mul(v, rest_q);
                    w->vx[i] = q_mul(w->vx[i], friction_q);
                }
            }
        }
    }
}

void world_read_y_float(const world_float_t *w, float *out) {
    memcpy(out, w->y, (size_t)w->s.n * sizeof(float));
}

void world_read_y_fixed(const world_fixed_t *w, float *out) {
    for (int32_t i = 0; i < w->s.n; i++) {
        out[i] = (float)w->y[i] / (float)Q_ONE;
    }
}

int32_t world_drain(const world_state_t *s, int32_t *out, int32_t n) {
    int32_t m = n < s->n_events ? n : s->n_events;
    for (int32_t k = 0; k < m; k++) {
        out[3 * k] = s->events[k].id;
        out[3 * k + 1] = s->events[k].kind;
        out[3 * k + 2] = s->events[k].speed;
    }
    return m < 0 ? 0 : m;
}

size_t world_float_size(void) {
    return sizeof(world_float_t);
}

size_t world_fixed_size(void) {
    return sizeof(world_fixed_t);
}
