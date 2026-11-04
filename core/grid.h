// A uniform grid (P598), the broad phase of the balls and of P592's bodies: cols x rows cells of cell_px, each item
// sorted into the cell of its centre by a counting sort (Ericson, Real-Time Collision Detection, 7.1). It knows no
// shape: a caller asks the cells around an item and tests the pairs itself. Plain C99 with no MicroPython or IDF
// header, so the Mac test builds it with clang and the board build with ESP-IDF (D15, D17).
// --- MVP (P598) ---
#ifndef GRID_H
#define GRID_H

#include <stddef.h>
#include <stdint.h>

// One grid in one block of grid_bytes(cols, rows, cap): this struct, then start (int32[cols * rows + 1]) and items
// (int32[cap]). Cell c's items are items[start[c]] to items[start[c + 1] - 1]. It stores no pointer, so the block may
// move with its owner.
typedef struct {
    int32_t cols, rows, cell_px, cap;
} grid_t;

// The bytes a grid of cols x rows cells and cap items takes.
size_t grid_bytes(int32_t cols, int32_t rows, int32_t cap);

// An empty grid. g is grid_bytes(cols, rows, cap) long; the caller keeps cols, rows and cap at 1 or more.
void grid_init(grid_t *g, int32_t cols, int32_t rows, int32_t cell_px, int32_t cap);

// The cell of a point: its column x / cell_px truncated and clamped to 0 to cols - 1, its row the same from y.
void grid_locate(const grid_t *g, float x, float y, int32_t *cx, int32_t *cy);

// Sort the n items (n at most cap) in: item i goes into grid_locate's cell of (x[i], y[i]). The last build is
// forgotten.
void grid_build(grid_t *g, const float *x, const float *y, int32_t n);

// Cell (cx, cy)'s items: *items points at the first, the count comes back; 0 for a cell outside the grid.
int32_t grid_cell(const grid_t *g, int32_t cx, int32_t cy, const int32_t **items);

#endif
