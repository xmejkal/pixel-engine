// The world's step (P590): world.h says what each function does. --- MVP (P590) ---
#include "world.h"

#include <string.h>

// The arrays after the struct, found from cap at each call.
#define X(w) ((float *)((w) + 1))
#define Y(w) (X(w) + (w)->cap)
#define VX(w) (Y(w) + (w)->cap)
#define VY(w) (VX(w) + (w)->cap)
#define REST_Y(w) (VY(w) + (w)->cap)
#define ASLEEP(w) ((uint8_t *)(REST_Y(w) + (w)->cap))

size_t world_bytes(int32_t cap) {  // cap is 1 to WORLD_MAX_BODIES: the binding checks it
    return sizeof(world_t) + (5 * sizeof(float) + sizeof(uint8_t)) * (size_t)cap;
}

void world_init(world_t *w, int32_t cap) {
    memset(w, 0, world_bytes(cap));
    w->cap = cap;
    memset(ASLEEP(w), 1, (size_t)cap);
}

int32_t world_spawn(world_t *w, int32_t i, float x, float y, float vx, float vy, float rest_y) {
    if (i < 0 || i >= w->cap) {
        return 0;
    }
    X(w)[i] = x;
    Y(w)[i] = y;
    VX(w)[i] = vx;
    VY(w)[i] = vy;
    REST_Y(w)[i] = rest_y;
    ASLEEP(w)[i] = 0;
    return 1;
}

static void add_event(world_t *w, int32_t id, int32_t kind, int32_t speed) {
    if (w->n_events == WORLD_MAX_EVENTS) {
        w->dropped++;
        return;
    }
    world_event_t *e = &w->events[w->n_events++];
    e->id = id;
    e->kind = kind;
    e->speed = speed;
}

void world_step(world_t *w, int32_t steps, float dt, float gravity, float restitution, float friction) {
    float *x = X(w), *y = Y(w), *vx = VX(w), *vy = VY(w), *rest_y = REST_Y(w);
    uint8_t *asleep = ASLEEP(w);
    float rest_speed = 1.5f * gravity * dt;  // fall_bodies' rest speed
    w->n_events = 0;
    w->dropped = 0;
    for (int32_t k = 0; k < steps; k++) {
        for (int32_t i = 0; i < w->cap; i++) {
            if (asleep[i]) {
                continue;
            }
            vy[i] += gravity * dt;
            x[i] += vx[i] * dt;
            y[i] += vy[i] * dt;
            if (y[i] >= rest_y[i]) {  // through the plane in this step: back onto it, so nothing tunnels
                y[i] = rest_y[i];
                float v = vy[i];
                if (v < rest_speed) {  // too slow to leave the plane again: it rests
                    vy[i] = 0.0f;
                    vx[i] = 0.0f;
                    asleep[i] = 1;
                    add_event(w, i, WORLD_EVENT_SLEEP, 0);
                } else {
                    add_event(w, i, WORLD_EVENT_HIT, (int32_t)v);
                    vy[i] = -v * restitution;
                    vx[i] *= friction;
                }
            }
        }
    }
}

int32_t world_read(const world_t *w, int16_t *out) {
    const float *x = X(w), *y = Y(w);
    const uint8_t *asleep = ASLEEP(w);
    for (int32_t i = 0; i < w->cap; i++) {
        out[3 * i] = (int16_t)x[i];
        out[3 * i + 1] = (int16_t)y[i];
        out[3 * i + 2] = asleep[i];
    }
    return w->cap;
}

int32_t world_drain(const world_t *w, int32_t *out, int32_t room) {
    int32_t m = room < w->n_events ? room : w->n_events;
    for (int32_t k = 0; k < m; k++) {
        out[3 * k] = w->events[k].id;
        out[3 * k + 1] = w->events[k].kind;
        out[3 * k + 2] = w->events[k].speed;
    }
    return m < 0 ? 0 : m;
}
