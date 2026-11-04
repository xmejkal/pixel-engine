// The uniform grid's counting sort (P598): grid.h says what each function does. --- MVP (P598) ---
#include "grid.h"

#include <string.h>

// The arrays after the struct, found from cols, rows and cap at each call.
#define START(g) ((int32_t *)((g) + 1))
#define ITEMS(g) (START(g) + (g)->cols * (g)->rows + 1)

size_t grid_bytes(int32_t cols, int32_t rows, int32_t cap) {
    return sizeof(grid_t) + sizeof(int32_t) * ((size_t)cols * (size_t)rows + 1 + (size_t)cap);
}

void grid_init(grid_t *g, int32_t cols, int32_t rows, int32_t cell_px, int32_t cap) {
    memset(g, 0, grid_bytes(cols, rows, cap));
    g->cols = cols;
    g->rows = rows;
    g->cell_px = cell_px;
    g->cap = cap;
}

void grid_locate(const grid_t *g, float x, float y, int32_t *cx, int32_t *cy) {
    int32_t c = (int32_t)(x / (float)g->cell_px), r = (int32_t)(y / (float)g->cell_px);
    *cx = c < 0 ? 0 : c >= g->cols ? g->cols - 1 : c;
    *cy = r < 0 ? 0 : r >= g->rows ? g->rows - 1 : r;
}

static int32_t cell_of(const grid_t *g, float x, float y) {
    int32_t cx, cy;
    grid_locate(g, x, y, &cx, &cy);
    return cy * g->cols + cx;
}

void grid_build(grid_t *g, const float *x, const float *y, int32_t n) {
    int32_t *start = START(g), *items = ITEMS(g), cells = g->cols * g->rows;
    memset(start, 0, sizeof(int32_t) * (size_t)(cells + 1));
    for (int32_t i = 0; i < n; i++) {  // count each cell's items
        start[cell_of(g, x[i], y[i])]++;
    }
    for (int32_t c = 1; c < cells; c++) {  // start[c] becomes the end of cell c
        start[c] += start[c - 1];
    }
    start[cells] = n;
    for (int32_t i = 0; i < n; i++) {  // place each item below its cell's end: start[c] ends as the cell's first
        items[--start[cell_of(g, x[i], y[i])]] = i;
    }
}

int32_t grid_cell(const grid_t *g, int32_t cx, int32_t cy, const int32_t **items) {
    if (cx < 0 || cx >= g->cols || cy < 0 || cy >= g->rows) {
        return 0;
    }
    const int32_t *start = START(g) + cy * g->cols + cx;
    *items = ITEMS(g) + start[0];
    return start[1] - start[0];
}
