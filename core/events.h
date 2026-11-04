// The event buffer (P590, its own header since P598): the facts of one step call, which Python drains once a frame
// and decides what they cause (D12, D13). The world (world.c) and the balls (fx_balls.c) share it. A step call keeps
// its first EVENTS_MAX events; the rest only count in dropped. Plain C99 with no MicroPython or IDF header.
// --- MVP (P598) ---
#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>

#define EVENTS_MAX 64

enum { EVENT_HIT = 1, EVENT_SLEEP = 2 };

typedef struct {
    int32_t id, kind, speed;  // speed: the impact in px/s, truncated toward 0; 0 for a sleep
} event_t;

// n and dropped first, so a reader that knows the owner's layout finds them at fixed bytes (tests/world_lib.py).
typedef struct {
    int32_t n, dropped;
    event_t events[EVENTS_MAX];
} event_buffer_t;

static inline void events_clear(event_buffer_t *b) {
    b->n = 0;
    b->dropped = 0;
}

static inline void events_add(event_buffer_t *b, int32_t id, int32_t kind, int32_t speed) {
    if (b->n == EVENTS_MAX) {
        b->dropped++;
        return;
    }
    event_t *e = &b->events[b->n++];
    e->id = id;
    e->kind = kind;
    e->speed = speed;
}

// Copy up to room of the events into out as id, kind, speed (3 int32 each); return how many.
static inline int32_t events_drain(const event_buffer_t *b, int32_t *out, int32_t room) {
    int32_t m = room < b->n ? room : b->n;
    for (int32_t k = 0; k < m; k++) {
        out[3 * k] = b->events[k].id;
        out[3 * k + 1] = b->events[k].kind;
        out[3 * k + 2] = b->events[k].speed;
    }
    return m < 0 ? 0 : m;
}

#endif
