// The fire (P595): a field of heat that rises and cools, drawn through a palette of 256 colours onto the whole panel.
// The method is Lode Vandevenne's heat fire (lodev.org/cgtutor/fire.html), written again from its formula only, with
// no code copied (D14). Plain C99 with no MicroPython or IDF header, so the Mac test builds it with clang and the board
// build with ESP-IDF.
// --- MVP (P595) ---
#ifndef FX_FIRE_H
#define FX_FIRE_H

#include <stdint.h>

#define FX_FIRE_MAX_SPOTS 16
#define FX_FIRE_MAX_EMBERS 24
// The cooling map's width and height, each a power of 2. 256 wide, so it never repeats across a field up to 256 wide (the
// games' fields are 160); a wider field, up to the binding's 1,024, repeats it every 256 cells
#define FX_FIRE_MAP_W 256
#define FX_FIRE_MAP_H 32
// --- MVP (P608) ---
// How fx_fire_draw and fx_fire_blend magnify a cell to its 2 x 2 px: OpenGL's GL_NEAREST and GL_LINEAR filters
#define FX_FIRE_NEAREST 0
#define FX_FIRE_LINEAR 1

// The caller owns heat (w x h bytes, row-major in game orientation, row h - 1 at the bottom) and cooling
// (FX_FIRE_MAP_W x FX_FIRE_MAP_H bytes, row-major). No static state.
typedef struct {
    int32_t w, h;
    uint8_t *heat;
    uint8_t *cooling;  // tiled over the field, its row (y + map_row) & 31 under field row y
    uint32_t rng;  // xorshift32 state, never 0
    int32_t map_row;  // 0 to 31, one more each step, so the cold patches rise with the flames
    int32_t fast_map_row;  // 0 to 31, two more each step: the map read again for the fast regions, which climb faster
    int32_t decay, cool, sway, spots, spread, fast, embers, fade;  // the look, see fx_fire_set_look
    int32_t spot_x[FX_FIRE_MAX_SPOTS];  // each hot spot's column
    int32_t ember_x[FX_FIRE_MAX_EMBERS], ember_y[FX_FIRE_MAX_EMBERS];  // in 1/256 of a cell
    int32_t ember_vx[FX_FIRE_MAX_EMBERS], ember_vy[FX_FIRE_MAX_EMBERS];  // in 1/256 of a cell a step
    int32_t ember_heat[FX_FIRE_MAX_EMBERS];  // 0 is a free slot
    int32_t base;  // 1: the step makes the bottom row; 0: it clears it (P596)
    int32_t filter;  // FX_FIRE_NEAREST or FX_FIRE_LINEAR, see fx_fire_set_filter (P608)
    int32_t bend, wander;  // the motion, see fx_fire_set_motion (P647)
    int32_t tick;  // steps taken, the bend's time
    int32_t map_dx;  // 0 to 65535: how far the maps drifted sideways, in 1/256 of a cell (the wander)
    int32_t drift_vx;  // -wander to wander: the drift's speed, in 1/256 of a cell a step
} fx_fire_t;

// Zero the field, seed the random numbers (a seed of 0 becomes 1), make the cooling map and place the spots. The look
// starts as part 1's: decay 31, cool 0, sway 0, spots 0, fast 0, embers 0, fade 0. The filter starts as NEAREST.
void fx_fire_init(fx_fire_t *f, uint8_t *heat, uint8_t *cooling, int32_t w, int32_t h, uint32_t seed);

// The look, each number checked by the caller. decay 0 to 255: 32 keeps the heat, less cools it, more spreads it but
// no cell grows hotter than the cells it reads. cool 0 to 255: how much the cooling map takes, 0 none. sway 0 or 1:
// each cell reads the cells below it shifted by -1, 0 or +1 at random. spots 0 to FX_FIRE_MAX_SPOTS: the hot spots at
// the base, 0 for a random bottom row. spread 1 to 64: a spot's half-width in cells. fast 0 to 255: a cell is fast
// when the map's cell at column (x + 128) & 255, row (y + fast_map_row) & 31 is at least 256 - fast, so 0 makes none.
// embers 0 to 255: an ember starts in embers of 256 steps, 0 none. fade 0 to 255: a loss every cell pays, fade
// sixteenths of a heat unit a row on top of the map's, so no parcel keeps its heat forever. A change of the spot count
// places the spots again.
void fx_fire_set_look(fx_fire_t *f, int32_t decay, int32_t cool, int32_t sway, int32_t spots, int32_t spread,
                      int32_t fast, int32_t embers, int32_t fade);

// steps steps with the look. Each one makes the bottom row (random, or the spots), then each cell above, from the top
// row down, becomes (the three cells below it + the cell two below) * decay >> 7, at most the hottest of those four,
// less the cooling map's cell * cool >> 8 and the fade, at least 0. Row y's fade is ((r + 1) * fade >> 4) -
// (r * fade >> 4), with r = h - 1 - y the rows above the bottom, so fade 4 costs one unit every fourth row. A fast
// cell reads the same four cells one row further down, so its heat climbs two rows a step, unless that puts its three
// cells past the bottom. A cell outside the field counts 0. wind -255 to 255: a row reads one column against the wind
// in |wind| of 256 steps, so positive wind blows the flames toward larger x. 0 takes no draw. After the rows an ember
// may start one row above the bottom, then each live ember moves, rising 2 to 5 rows a step, loses 12 heat, dies under
// 40 heat or outside the field, and writes its heat into its cell where that is hotter.
void fx_fire_step(fx_fire_t *f, int32_t steps, int32_t wind);

// The whole panel, 2h px wide and 2w px high, as palette[heat], in the panel order of lcd.py's TftCanvas.fill at
// ORIENTATION 0: panel (px, py) shows cell ((2w - 1 - py) / 2, px / 2). The palette is already in the panel's byte order.
void fx_fire_draw(const fx_fire_t *f, uint16_t *panel, const uint16_t *palette);

// --- MVP (P603) ---
// The same panel blended over what it holds (the picture mode, over the painted sky): each pixel becomes
// alpha_blend_565(palette[heat], pixel, alpha[heat]) of driver/src/blend565.h. A cell of alpha 0 leaves its pixels.
// LINEAR skips the heats of a cell whose neighbourhood is too cold for any alpha but 0, with the same output (P646).
void fx_fire_blend(const fx_fire_t *f, uint16_t *panel, const uint16_t *palette, const uint8_t *alpha);

// --- MVP (P596) ---
// on 1 (init's): each step makes the bottom row, from the spots or at random. on 0: each step clears the bottom row
// instead and takes no draw for it, so only what fx_fire_burn puts in burns (bodies on fire).
void fx_fire_set_base(fx_fire_t *f, int32_t on);

// Heat put in along the segment from cell (x0, y0) to (x1, y1): at each of its max(|dx|, |dy|) + 1 points, every
// cell within r (dx * dx + dy * dy <= r * r) and inside the field keeps the larger of its heat and heat. The ends
// may lie outside the field.
void fx_fire_burn(fx_fire_t *f, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r, uint8_t heat);

// --- MVP (P608) ---
// filter FX_FIRE_NEAREST (init's) or FX_FIRE_LINEAR, checked by the caller. NEAREST paints each cell as its 2 x 2 px.
// LINEAR paints them with bilinear filtering, GL_LINEAR's weights at 2x: game pixel (2x + i, 2y + j), i and j 0 or 1,
// sits a quarter cell from cell (x, y)'s centre toward cell (x + 2i - 1, y + 2j - 1), so its heat is
// (9a + 3b + 3c + d + 8) >> 4, with a its own cell, b and c the side neighbours toward it and d the diagonal one. A
// neighbour past the field's edge is the edge cell (GL_CLAMP_TO_EDGE). The panel map stays: panel (px, py) shows game
// pixel (2w - 1 - py, px). The blend leaves a pixel of alpha 0 as it is.
void fx_fire_set_filter(fx_fire_t *f, int32_t filter);

// --- Later, not MVP: kept for other games, the PO 2026-11-04 (P602) ---
// The world moves under the screen by dx cells, -1024 to 1024 checked by the caller: every row shifts by dx (negative
// is left, as a side-scroller's world moves), the cells it uncovers become 0, and |dx| >= w clears the field. Each
// live ember moves by dx too. It takes no draw.
void fx_fire_scroll(fx_fire_t *f, int32_t dx);

// --- Later, not MVP: a prototype for the PO's pick (P647) ---
// The motion, two settings that add up, both 0 after init, each checked by the caller. bend 0 to 8 (domain warping,
// iquilezles.org/articles/warp, written from the description): a sine wave of bend cells, FX_FIRE_BEND_ROWS rows
// long, travels up a wave every FX_FIRE_BEND_STEPS steps. Row y reads both maps at column x + D(y) and the heat k rows
// below at c + D(y) - D(y + k), so the cold gaps and the flames follow the bent path. wander 0 to 255 (texture
// panning): the drift's speed walks at random within +-wander, in 1/256 of a cell a step, and moves the cooling map
// one way and the fast map the other, so the gaps switch around and the fast patches cross them.
#define FX_FIRE_BEND_ROWS 40
#define FX_FIRE_BEND_STEPS 50
void fx_fire_set_motion(fx_fire_t *f, int32_t bend, int32_t wander);

#endif
