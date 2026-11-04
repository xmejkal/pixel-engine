// The balls (P598): up to cap round balls that fall, knock and pile in a 320 x 240 box, as an effects kernel (D8).
// The solver is Jakobsen's position Verlet with relaxation passes ("Advanced Character Physics", GDC 2001), written
// from the method, no code copied (D14): each substep integrates, then pushes every overlapping pair apart along its
// normal, split by mass (r squared), over FX_BALLS_PASSES passes, keeps each ball inside the walls and caps its move.
// The pairs come from the uniform grid of grid.h, cell FX_BALLS_CELL px, the largest diameter. A hit is a fact in the
// event buffer (events.h) for Python to drain (D12). Plain C99 with no MicroPython or IDF header, so the Mac test
// builds it with clang and the board build with ESP-IDF (D15, D17).
// --- MVP (P598) ---
#ifndef FX_BALLS_H_  // FX_BALLS_H is the box height below
#define FX_BALLS_H_

#include <stddef.h>
#include <stdint.h>

#include "events.h"

#define FX_BALLS_W 320  // the box, the panel's game size (lcd.py's ORIENTATION 0)
#define FX_BALLS_H 240
#define FX_BALLS_R_MIN 2  // a ball's radius in px
#define FX_BALLS_R_MAX 5
#define FX_BALLS_COLOURS 16  // a ball's colour is an index into fx_balls_draw's palette of this many
#define FX_BALLS_CELL (2 * FX_BALLS_R_MAX)  // the grid's cell: two touching centres are at most a cell apart
#define FX_BALLS_COLS (FX_BALLS_W / FX_BALLS_CELL)
#define FX_BALLS_ROWS (FX_BALLS_H / FX_BALLS_CELL)
#define FX_BALLS_PASSES 2  // relaxation passes a substep: one left 1.29 px of overlap in the plan's simulation
#define FX_BALLS_MOVE_MAX 1.5f  // a ball's whole move in a substep, in px, so no touching pair can swap sides

// One kernel in one block of fx_balls_bytes(cap): this struct, then the grid (grid_bytes(COLS, ROWS, cap)), then x, y,
// px, py, vx, vy (float[cap] each, px and py the place at the substep's start, vx and vy in px/s) and r and colour
// (uint8_t[cap] each) last. It stores no pointer, so the block may move with its owner.
typedef struct {
    int32_t cap, n;  // room, and the balls spawned so far
    event_buffer_t events;  // the last step call's hits: a step call clears them first
    uint8_t span[FX_BALLS_R_MAX + 1][FX_BALLS_R_MAX + 1];  // span[r][dx]: a disc of radius r's half-height dx from its centre
} fx_balls_t;

// The bytes a kernel of cap balls takes.
size_t fx_balls_bytes(int32_t cap);

// No balls, no events, the span table made. b is fx_balls_bytes(cap) long; the caller keeps cap at 1 or more.
void fx_balls_init(fx_balls_t *b, int32_t cap);

// One more ball at (x, y) moving at (vx, vy) px/s, radius r (FX_BALLS_R_MIN to FX_BALLS_R_MAX) and colour (0 to
// FX_BALLS_COLOURS - 1), both checked by the caller. Return its id, or -1 when the kernel is full.
int32_t fx_balls_spawn(fx_balls_t *b, float x, float y, float vx, float vy, int32_t r, int32_t colour);

// No balls: n back to 0.
void fx_balls_clear(fx_balls_t *b);

// --- MVP (P598): the pit's shake, the PO 2026-11-04 ---
// Every ball's velocity plus (vx, vy) px/s, as if the box were bumped. The move cap of a substep still holds, so a ball
// leaves at most FX_BALLS_MOVE_MAX px a substep.
void fx_balls_kick(fx_balls_t *b, float vx, float vy);

// Clear the events, then steps steps of dt s, each in substeps substeps of dt / substeps. A substep: each ball's
// velocity gains gravity * dt and its place moves by it (the velocity now is the one every hit speed reads); the grid
// is built; FX_BALLS_PASSES passes each push every overlapping pair apart along its normal, the lighter ball further
// (mass r squared; a pair closer than 1e-3 px takes the normal (0, -1)), and put each ball back inside the walls. In
// the first pass only, a pair closing at hit_min px/s or more along its normal adds (id of the lower index, EVENT_HIT,
// that speed), and a ball into a wall at hit_min or more adds (id, EVENT_HIT, speed). A wall takes the ball's move into
// it, so it stops there (no bounce). Last, a ball's whole move in the substep is capped at FX_BALLS_MOVE_MAX px, and
// its velocity becomes its move over the substep's dt, less the damping: damping is the part of its speed a ball keeps
// over one whole step (1 keeps all), spread evenly over the substeps, so 3 or 4 substeps fall alike. The cap holds a
// fall to FX_BALLS_MOVE_MAX * substeps / dt px/s (600 at 4 substeps of a 10 ms step), under free fall from the top.
void fx_balls_step(fx_balls_t *b, int32_t steps, int32_t substeps, float dt, float gravity, float damping,
                   float hit_min);

// Each ball's x, y and r into out as 3 int16 (x and y truncated toward 0); return n.
int32_t fx_balls_read(const fx_balls_t *b, int16_t *out);

// Copy up to room of the last step call's events into out as id, kind, speed (3 int32 each); return how many.
int32_t fx_balls_drain(const fx_balls_t *b, int32_t *out, int32_t room);

// The whole panel (FX_BALLS_H px wide, FX_BALLS_W px high): bg everywhere, then each ball as a filled disc of
// palette[colour] at its truncated centre, in the panel order of lcd.py's TftCanvas.fill at ORIENTATION 0: game
// (x, y) is panel (y, FX_BALLS_W - 1 - x). The colours are already in the panel's byte order.
void fx_balls_draw(const fx_balls_t *b, uint16_t *panel, const uint16_t *palette, uint16_t bg);

#endif
