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
// The cooling map's width and height, each a power of 2. 256 wide, so it never repeats across a field up to 256 wide
#define FX_FIRE_MAP_W 256
#define FX_FIRE_MAP_H 32

// The caller owns heat (w x h bytes, row-major in game orientation, row h - 1 at the bottom) and cooling
// (FX_FIRE_MAP_W x FX_FIRE_MAP_H bytes, row-major). No static state.
typedef struct {
    int32_t w, h;
    uint8_t *heat;
    uint8_t *cooling;  // tiled over the field, its row (y + scroll) & 31 under field row y
    uint32_t rng;  // xorshift32 state, never 0
    int32_t scroll;  // 0 to 31, one more each step, so the cold patches rise with the flames
    int32_t fast_scroll;  // 0 to 31, two more each step: the map read again for the fast regions, which climb faster
    int32_t decay, cool, sway, spots, spread, fast, embers, fade;  // the look, see fx_fire_set_look
    int32_t spot_x[FX_FIRE_MAX_SPOTS];  // each hot spot's column
    int32_t ember_x[FX_FIRE_MAX_EMBERS], ember_y[FX_FIRE_MAX_EMBERS];  // in 1/256 of a cell
    int32_t ember_vx[FX_FIRE_MAX_EMBERS], ember_vy[FX_FIRE_MAX_EMBERS];  // in 1/256 of a cell a step
    int32_t ember_heat[FX_FIRE_MAX_EMBERS];  // 0 is a free slot
} fx_fire_t;

// Zero the field, seed the random numbers (a seed of 0 becomes 1), make the cooling map and place the spots. The look
// starts as part 1's: decay 31, cool 0, sway 0, spots 0, fast 0, embers 0, fade 0.
void fx_fire_init(fx_fire_t *f, uint8_t *heat, uint8_t *cooling, int32_t w, int32_t h, uint32_t seed);

// The look, each number checked by the caller. decay 0 to 255: 32 keeps the heat, less cools it, more spreads it but
// no cell grows hotter than the cells it reads. cool 0 to 255: how much the cooling map takes, 0 none. sway 0 or 1:
// each cell reads the cells below it shifted by -1, 0 or +1 at random. spots 0 to FX_FIRE_MAX_SPOTS: the hot spots at
// the base, 0 for a random bottom row. spread 1 to 64: a spot's half-width in cells. fast 0 to 255: a cell is fast
// when the map's cell at column (x + 128) & 255, row (y + fast_scroll) & 31 is at least 256 - fast, so 0 makes none.
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

#endif
