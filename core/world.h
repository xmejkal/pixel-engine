// The engine's world (P589 probe): up to WORLD_MAX_BODIES falling bodies as parallel arrays, stepped with the maths of
// games/motion.py's fall_bodies (p582-heavy), in float32 or in Q16.16 fixed point. Plain C99 with no MicroPython or
// IDF header, so the Mac test builds it with clang and the board build with ESP-IDF.
// --- probe, throwaway (P589) ---
#ifndef WORLD_H
#define WORLD_H

#include <stddef.h>
#include <stdint.h>

#define WORLD_MAX_BODIES 256  // 64 in P589; 256 for the rain (P593): 12,320 B of static RAM for the two worlds
#define WORLD_MAX_EVENTS 64  // a step call keeps its first 64 events; the rest only count in dropped

enum { WORLD_EVENT_HIT = 1, WORLD_EVENT_SLEEP = 2 };

typedef struct {
    int32_t id, kind, speed;  // speed: the impact in px/s, truncated as fall_bodies' int(v); 0 for a sleep
} world_event_t;

// What both modes share, first in each world, so one drain reads either.
typedef struct {
    int32_t n;
    uint32_t awake_steps;  // body-steps stepped awake since the load: the bounce row's divisor
    uint8_t asleep[WORLD_MAX_BODIES];
    int32_t n_events, dropped;  // the last step call's events: a step call clears them first, as fall_bodies clears hit
    world_event_t events[WORLD_MAX_EVENTS];
} world_state_t;

typedef struct {
    world_state_t s;
    float x[WORLD_MAX_BODIES], y[WORLD_MAX_BODIES], vx[WORLD_MAX_BODIES], vy[WORLD_MAX_BODIES];
    float rest_y[WORLD_MAX_BODIES];
} world_float_t;

// Q16.16: 16 bits of whole px, so every value stays under 32,768 px (or px/s).
typedef struct {
    world_state_t s;
    int32_t x[WORLD_MAX_BODIES], y[WORLD_MAX_BODIES], vx[WORLD_MAX_BODIES], vy[WORLD_MAX_BODIES];
    int32_t rest_y[WORLD_MAX_BODIES];
} world_fixed_t;

// Load n bodies (n is clamped to WORLD_MAX_BODIES), all awake, with no events.
void world_load_float(world_float_t *w, int32_t n, const float *x, const float *y, const float *vx, const float *vy,
    const float *rest_y);
void world_load_fixed(world_fixed_t *w, int32_t n, const float *x, const float *y, const float *vx, const float *vy,
    const float *rest_y);

// steps steps of 1 / rate s each. Float takes dt = 1.0f / rate; fixed divides by the integer rate, so no Q16.16 dt
// rounds. restitution and friction multiply a speed at each bounce.
void world_step_float(world_float_t *w, int32_t steps, int32_t rate, float gravity, float restitution, float friction);
void world_step_fixed(world_fixed_t *w, int32_t steps, int32_t rate, float gravity, float restitution, float friction);

void world_read_y_float(const world_float_t *w, float *out);
void world_read_y_fixed(const world_fixed_t *w, float *out);

// Put body i in the float world at this state, awake (P593). n becomes i + 1 when i is at or past n, and the bodies
// skipped over sleep at 0. Return 1, or 0 when i is under 0 or at or past WORLD_MAX_BODIES (nothing changes then).
int32_t world_spawn_float(world_float_t *w, int32_t i, float x, float y, float vx, float vy, float rest_y);

// Each body's x, y and asleep into out as 3 int16 (x and y truncated toward 0, and inside +-32,767 px), so Python
// makes no float (P593).
void world_read_float(const world_float_t *w, int16_t *out);

// Copy up to n of the last step call's events into out as id, kind, speed (3 ints each); return how many.
int32_t world_drain(const world_state_t *s, int32_t *out, int32_t n);

// For ctypes on the Mac: the bytes a world takes.
size_t world_float_size(void);
size_t world_fixed_size(void);

#endif
