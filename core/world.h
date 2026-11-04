// The engine's world (P590): one world_t per engine.Bodies, cap falling bodies as parallel arrays, stepped with the
// maths of games/motion.py's fall_bodies (its twin). A step reports its facts as events (a hit with its speed, a body
// that comes to rest) for Python to drain once a frame and decide what they cause (D12, D13). Plain C99 with no
// MicroPython or IDF header, so the Mac test builds it with clang and the board build with ESP-IDF (D15, D17).
// --- MVP (P590) ---
#ifndef WORLD_H
#define WORLD_H

#include <stddef.h>
#include <stdint.h>

#define WORLD_MAX_EVENTS 64  // a step call keeps its first 64 events; the rest only count in dropped

enum { WORLD_EVENT_HIT = 1, WORLD_EVENT_SLEEP = 2 };

typedef struct {
    int32_t id, kind, speed;  // speed: the impact in px/s, truncated as fall_bodies' int(v); 0 for a sleep
} world_event_t;

// One world in one block of world_bytes(cap): this struct, then x, y, vx, vy and rest_y (float[cap] each) and asleep
// (uint8_t[cap]) last, so every array stays aligned. It stores no pointer: each call finds its arrays from cap, so
// the block may move with its owner.
typedef struct {
    int32_t cap;  // bodies, fixed at world_init
    int32_t n_events, dropped;  // the last step call's events: a step call clears them first
    world_event_t events[WORLD_MAX_EVENTS];
} world_t;

// The bytes a world of cap bodies takes: sizeof(world_t) + 21 * cap.
size_t world_bytes(int32_t cap);

// cap bodies, each asleep at (0, 0) with no speed, and no events. w is world_bytes(cap) long.
void world_init(world_t *w, int32_t cap);

// Body i at this state, awake. Return 1, or 0 when i is outside 0 to cap - 1 (nothing changes then).
int32_t world_spawn(world_t *w, int32_t i, float x, float y, float vx, float vy, float rest_y);

// Clear the events, then steps steps of dt s each. A body falls under gravity; at its plane it bounces back up with
// restitution times its speed and friction times its vx, an event (i, HIT, speed); under 1.5 * gravity * dt it rests
// there instead, an event (i, SLEEP, 0), and a resting body moves no more until a spawn.
void world_step(world_t *w, int32_t steps, float dt, float gravity, float restitution, float friction);

// Each body's x, y and asleep into out as 3 int16 (x and y truncated toward 0), so Python makes no float. The caller
// keeps x and y inside +-32,767 px: nothing here clamps them. Return cap.
int32_t world_read(const world_t *w, int16_t *out);

// Copy up to room of the last step call's events into out as id, kind, speed (3 int32 each); return how many.
int32_t world_drain(const world_t *w, int32_t *out, int32_t room);

#endif
