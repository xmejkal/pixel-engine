// The fire (P595): see fx_fire.h. --- MVP (P595) ---
#include "fx_fire.h"
#include "blend565.h"  // driver/src: the blend s3lcd uses, plain C (P603)

#include <string.h>

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

// The random draws, in this order, so a reference can repeat them byte for byte. Each draw is one xorshift32 call.
// - fx_fire_init: the map's 8192 cells row by row, one draw each, then the FX_FIRE_MAX_SPOTS spots, one draw each.
// - fx_fire_set_look, when the spot count changes: the FX_FIRE_MAX_SPOTS spots again, one draw each.
// - fx_fire_step, per step: with the base off (P596), no draw for the bottom row. Else with spots 0, the bottom row's cells left to right, one draw each (part 1's row). Else
//   each spot in turn, one draw (its walk or jump), then the bottom row's cells left to right, one draw each (the
//   flicker). Then, with sway 1, the rows from the top down, one draw per 16 cells from x = 0, 16, 32 and so on.
//   With wind not 0, each row takes one draw first, before its sway draws (the wind's). Then, with embers not 0,
//   one draw after the rows (the ember's).
// A spot's place is r % w. A spot's draw r: when its top 6 bits are 0 (1 in 64) it jumps to (r >> 8) % w, else it
// walks r % 3 - 1 cells, clamped to the field. The flicker keeps heat * (128 + (r >> 25)) >> 8. The sway's draw gives
// 2 bits per cell, the lowest first: 00 is -1, 01 and 10 are 0, 11 is +1. The wind's draw r shifts the row's read one
// column against the wind when r & 255 is under |wind|. The ember's draw r starts one in the first free slot when
// r & 255 is under embers: at the spot ((r >> 8) & 255) % spots (the column (r >> 8) % w with spots 0), vy
// -(2 + ((r >> 16) & 3)) rows a step and vx ((r >> 18) & 127) - 64 in 1/256 of a cell, plus wind / 8.

static void place_spots(fx_fire_t *f) {
    for (int32_t i = 0; i < FX_FIRE_MAX_SPOTS; i++) {
        f->spot_x[i] = (int32_t)(xorshift32(&f->rng) % (uint32_t)f->w);
    }
}

// n cells stride apart, each the mean of itself and its two neighbours, in place, wrapping around the ends.
static void blur_ring(uint8_t *p, int32_t n, int32_t stride) {
    uint32_t first = p[0], prev = p[(n - 1) * stride];
    for (int32_t i = 0; i < n; i++) {
        uint32_t cur = p[i * stride];
        uint32_t next = i + 1 < n ? p[(i + 1) * stride] : first;
        p[i * stride] = (uint8_t)((prev + cur + next) / 3);
        prev = cur;
    }
}

// The cooling map: 1 cell in 16 at 255 and the rest 0, two 3 x 3 box blurs that wrap (each a row pass, then a
// column pass), so the tile has no seam, then its min to max stretched onto 0 to 255.
static void make_cooling(fx_fire_t *f) {
    uint8_t *map = f->cooling;
    int32_t n = FX_FIRE_MAP_W * FX_FIRE_MAP_H;
    for (int32_t i = 0; i < n; i++) {
        map[i] = xorshift32(&f->rng) >> 28 == 0 ? 255 : 0;
    }
    for (int32_t pass = 0; pass < 2; pass++) {
        for (int32_t k = 0; k < FX_FIRE_MAP_H; k++) {
            blur_ring(map + k * FX_FIRE_MAP_W, FX_FIRE_MAP_W, 1);
        }
        for (int32_t k = 0; k < FX_FIRE_MAP_W; k++) {
            blur_ring(map + k, FX_FIRE_MAP_H, FX_FIRE_MAP_W);
        }
    }
    uint32_t lo = 255, hi = 0;
    for (int32_t i = 0; i < n; i++) {
        lo = map[i] < lo ? map[i] : lo;
        hi = map[i] > hi ? map[i] : hi;
    }
    for (int32_t i = 0; i < n; i++) {
        map[i] = hi > lo ? (uint8_t)((map[i] - lo) * 255 / (hi - lo)) : 0;
    }
}

void fx_fire_init(fx_fire_t *f, uint8_t *heat, uint8_t *cooling, int32_t w, int32_t h, uint32_t seed) {
    f->w = w;
    f->h = h;
    f->heat = heat;
    f->cooling = cooling;
    f->rng = seed ? seed : 1;
    f->map_row = 0;
    f->fast_map_row = 0;
    f->decay = 31;
    f->cool = 0;
    f->sway = 0;
    f->spots = 0;
    f->spread = 1;
    f->fast = 0;
    f->embers = 0;
    f->fade = 0;
    f->base = 1;
    f->filter = FX_FIRE_NEAREST;  // P608
    f->born = 0;  // P747
    for (int32_t i = 0; i < FX_FIRE_MAX_RISES; i++) {
        f->born_rise[i] = 0;  // P751
    }
    memset(f->ember_heat, 0, sizeof f->ember_heat);
    memset(heat, 0, (size_t)w * (size_t)h);
    make_cooling(f);
    place_spots(f);
}

void fx_fire_set_look(fx_fire_t *f, int32_t decay, int32_t cool, int32_t sway, int32_t spots, int32_t spread,
                      int32_t fast, int32_t embers, int32_t fade) {
    f->decay = decay;
    f->cool = cool;
    f->sway = sway;
    f->spread = spread;
    f->fast = fast;
    f->embers = embers;
    f->fade = fade;
    if (spots != f->spots) {
        f->spots = spots;
        place_spots(f);
    }
}

// The bottom row from the spots: each walks or jumps, each writes a bump (the larger value kept where bumps meet),
// then each cell flickers to 50 to 100 % of its heat.
static void spots_row(fx_fire_t *f, uint8_t *bottom) {
    int32_t w = f->w, spread = f->spread;
    for (int32_t i = 0; i < f->spots; i++) {
        uint32_t r = xorshift32(&f->rng);
        int32_t x = f->spot_x[i];
        if (r >> 26 == 0) {
            x = (int32_t)((r >> 8) % (uint32_t)w);
        } else {
            x += (int32_t)(r % 3) - 1;
            x = x < 0 ? 0 : x >= w ? w - 1 : x;
        }
        f->spot_x[i] = x;
    }
    memset(bottom, 0, (size_t)w);
    for (int32_t i = 0; i < f->spots; i++) {
        for (int32_t dx = -spread; dx <= spread; dx++) {
            int32_t x = f->spot_x[i] + dx;
            if (x >= 0 && x < w) {
                uint8_t v = (uint8_t)(255 - (dx < 0 ? -dx : dx) * 255 / (spread + 1));
                bottom[x] = v > bottom[x] ? v : bottom[x];
            }
        }
    }
    for (int32_t x = 0; x < w; x++) {
        bottom[x] = (uint8_t)((bottom[x] * (128 + (xorshift32(&f->rng) >> 25))) >> 8);
    }
}

// The ember's draw, then each live ember moves, cools, dies or writes its heat into its cell (the larger value kept).
static void embers_step(fx_fire_t *f, int32_t wind) {
    int32_t w = f->w, h = f->h;
    uint32_t r = f->embers > 0 ? xorshift32(&f->rng) : 0;
    if ((r & 255) < (uint32_t)f->embers) {
        for (int32_t i = 0; i < FX_FIRE_MAX_EMBERS; i++) {
            if (f->ember_heat[i] == 0) {
                int32_t x = f->spots > 0 ? f->spot_x[((r >> 8) & 255) % (uint32_t)f->spots]
                                         : (int32_t)((r >> 8) % (uint32_t)w);
                f->ember_x[i] = x * 256;
                f->ember_y[i] = (h - 2) * 256;
                f->ember_vx[i] = (int32_t)((r >> 18) & 127) - 64 + wind / 8;
                f->ember_vy[i] = -(2 + (int32_t)((r >> 16) & 3)) * 256;
                f->ember_heat[i] = 255;
                if (f->born < FX_FIRE_MAX_RISES) {
                    f->born_rise[f->born] = -f->ember_vy[i] / 256;  // P751
                }
                f->born++;  // P747
                break;
            }
        }
    }
    for (int32_t i = 0; i < FX_FIRE_MAX_EMBERS; i++) {
        if (f->ember_heat[i] == 0) {
            continue;
        }
        int32_t x = f->ember_x[i] += f->ember_vx[i];
        int32_t y = f->ember_y[i] += f->ember_vy[i];
        int32_t heat = f->ember_heat[i] -= 12;
        if (heat < 40 || x < 0 || y < 0 || x >= w * 256) {
            f->ember_heat[i] = 0;
            continue;
        }
        uint8_t *at = f->heat + (y / 256) * w + x / 256;
        *at = *at > heat ? *at : (uint8_t)heat;
    }
}

static inline uint32_t cell(const uint8_t *row, int32_t x, int32_t w) {  // 0 outside the field
    return (uint32_t)x < (uint32_t)w ? row[x] : 0;
}

void fx_fire_step(fx_fire_t *f, int32_t steps, int32_t wind) {
    int32_t w = f->w, h = f->h;
    uint32_t decay = (uint32_t)f->decay, cool = (uint32_t)f->cool, fade = (uint32_t)f->fade;
    uint32_t fast_at = 256 - (uint32_t)f->fast;  // a map cell at least this makes a fast cell, 256 never
    uint32_t gust = (uint32_t)(wind < 0 ? -wind : wind);
    int32_t against = wind > 0 ? -1 : 1;  // a cell reads upwind, so positive wind carries heat to larger x
    uint8_t *bottom = f->heat + (h - 1) * w;
    for (int32_t s = 0; s < steps; s++) {
        if (!f->base) {
            memset(bottom, 0, (size_t)w);
        } else if (f->spots > 0) {
            spots_row(f, bottom);
        } else {
            for (int32_t x = 0; x < w; x++) {
                bottom[x] = (uint8_t)(xorshift32(&f->rng) >> 24);
            }
        }
        for (int32_t y = 0; y < h - 1; y++) {  // top down, so each row reads rows this step has not changed yet
            uint8_t *row = f->heat + y * w;
            const uint8_t *below = row + w;
            const uint8_t *below2 = y + 2 < h ? below + w : NULL;
            const uint8_t *below3 = y + 3 < h ? below2 + w : NULL;
            const uint8_t *cooling = f->cooling + ((y + f->map_row) & (FX_FIRE_MAP_H - 1)) * FX_FIRE_MAP_W;
            const uint8_t *fast = below2 ? f->cooling + ((y + f->fast_map_row) & (FX_FIRE_MAP_H - 1)) * FX_FIRE_MAP_W
                                         : NULL;
            uint32_t up = (uint32_t)(h - 1 - y);  // rows above the bottom
            uint32_t faded = (((up + 1) * fade) >> 4) - ((up * fade) >> 4);  // fade / 16 a row, in whole units
            int32_t shift = gust && (xorshift32(&f->rng) & 255) < gust ? against : 0;
            uint32_t bits = 0;
            for (int32_t x = 0; x < w; x++) {
                int32_t c = x + shift;  // the column read below, shifted by the wind and the sway
                if (f->sway) {
                    if ((x & 15) == 0) {
                        bits = xorshift32(&f->rng);
                    }
                    c += (int32_t)((bits >> 1) & 1) + (int32_t)(bits & 1) - 1;
                    bits >>= 2;
                }
                const uint8_t *near = below, *far = below2;  // a fast cell reads one row further down
                if (fast && fast[(x + FX_FIRE_MAP_W / 2) & (FX_FIRE_MAP_W - 1)] >= fast_at) {
                    near = below2;
                    far = below3;
                }
                uint32_t left = cell(near, c - 1, w), mid = cell(near, c, w), right = cell(near, c + 1, w);
                uint32_t two = far ? cell(far, c, w) : 0;
                uint32_t hot = left > mid ? left : mid;  // the hottest cell read
                hot = right > hot ? right : hot;
                hot = two > hot ? two : hot;
                uint32_t sum = ((left + mid + right + two) * decay) >> 7;
                sum = sum > hot ? hot : sum;  // never hotter than its source, so no decay runs away
                uint32_t loss = ((cooling[x & (FX_FIRE_MAP_W - 1)] * cool) >> 8) + faded;
                row[x] = (uint8_t)(sum > loss ? sum - loss : 0);
            }
        }
        embers_step(f, wind);
        f->map_row = (f->map_row + 1) & (FX_FIRE_MAP_H - 1);
        f->fast_map_row = (f->fast_map_row + 2) & (FX_FIRE_MAP_H - 1);
    }
}

// --- MVP (P608) ---
// LINEAR down panel row py (see fx_fire_set_filter). The row shows game column gx = 2w - 1 - py, which mixes its cell
// column x = gx / 2 3:1 with the neighbour column on its side, m = 3a + b, once a cell. A cell's two pixels then mix
// its m 3:1 with the m above or below: (3m + m' + 8) >> 4 is (9a + 3b + 3c + d + 8) >> 4. Both clamp to the edge. Sets
// col and side to the two columns' top cells.
static inline void linear_columns(const fx_fire_t *f, int32_t py, const uint8_t **col, const uint8_t **side) {
    int32_t w = f->w, gx = 2 * w - 1 - py, x = gx >> 1, nx = x + (gx & 1 ? 1 : -1);
    nx = nx < 0 ? 0 : nx >= w ? w - 1 : nx;
    *col = f->heat + x;
    *side = f->heat + nx;
}

void fx_fire_draw(const fx_fire_t *f, uint16_t *panel, const uint16_t *palette) {
    int32_t w = f->w, h = f->h, pw = 2 * h;
    if (f->filter == FX_FIRE_LINEAR) {  // P608: the two panel rows of a cell differ, so no memcpy
        for (int32_t py = 0; py < 2 * w; py++) {
            const uint8_t *col, *side;
            linear_columns(f, py, &col, &side);
            uint16_t *out = panel + py * pw;
            uint32_t m = 3u * col[0] + side[0], up = m;  // above the top cell: the top cell
            for (int32_t y = 0; y < h; y++, col += w, side += w) {
                uint32_t down = y + 1 < h ? 3u * col[w] + side[w] : m;
                out[2 * y] = palette[(3u * m + up + 8u) >> 4];
                out[2 * y + 1] = palette[(3u * m + down + 8u) >> 4];
                up = m;
                m = down;
            }
        }
        return;
    }
    for (int32_t k = 0; k < w; k++) {  // panel rows 2k and 2k + 1 show field column w - 1 - k, top cell first
        const uint8_t *col = f->heat + (w - 1 - k);
        uint16_t *out = panel + 2 * k * pw;
        for (int32_t y = 0; y < h; y++, col += w) {
            uint16_t c = palette[*col];
            out[2 * y] = c;
            out[2 * y + 1] = c;
        }
        memcpy(out + pw, out, (size_t)pw * sizeof(uint16_t));
    }
}

// --- MVP (P603) ---
void fx_fire_blend(const fx_fire_t *f, uint16_t *panel, const uint16_t *palette, const uint8_t *alpha) {
    int32_t w = f->w, h = f->h, pw = 2 * h;
    if (f->filter == FX_FIRE_LINEAR) {  // --- MVP (P608) --- the heats of fx_fire_draw's LINEAR, each pixel blended
        // --- MVP (P646) --- every heat below first_visible_heat has alpha 0 (256: every heat). A pixel's heat
        // (3m + m' + 8) >> 4 stays below T = first_visible_heat while m and m' are at most 4T - 3, since
        // (16T - 12 + 8) >> 4 = T - 1. So a cell whose up, m and down are all that small skips its two pixels' heats.
        uint32_t first_visible_heat = 0;
        while (first_visible_heat < 256 && alpha[first_visible_heat] == 0) {
            first_visible_heat++;
        }
        uint32_t clear_limit = 4u * first_visible_heat;  // skip while the largest mix + 3 is at most this; 0 skips none
        for (int32_t py = 0; py < 2 * w; py++) {
            const uint8_t *col, *side;
            linear_columns(f, py, &col, &side);
            uint16_t *out = panel + py * pw;
            uint32_t m = 3u * col[0] + side[0], up = m;
            for (int32_t y = 0; y < h; y++, col += w, side += w) {
                uint32_t down = y + 1 < h ? 3u * col[w] + side[w] : m;
                uint32_t most = up > m ? up : m;
                most = down > most ? down : most;
                if (most + 3u > clear_limit) {
                    uint32_t h0 = (3u * m + up + 8u) >> 4, h1 = (3u * m + down + 8u) >> 4;
                    if (alpha[h0] != 0) {  // a pixel of alpha 0 keeps what it holds, and skips the blend's cost
                        out[2 * y] = alpha_blend_565(palette[h0], out[2 * y], alpha[h0]);
                    }
                    if (alpha[h1] != 0) {
                        out[2 * y + 1] = alpha_blend_565(palette[h1], out[2 * y + 1], alpha[h1]);
                    }
                }
                up = m;
                m = down;
            }
        }
        return;
    }
    for (int32_t k = 0; k < w; k++) {  // the map of fx_fire_draw: panel rows 2k and 2k + 1 show column w - 1 - k
        const uint8_t *col = f->heat + (w - 1 - k);
        uint16_t *out = panel + 2 * k * pw;
        for (int32_t y = 0; y < h; y++, col += w) {
            uint8_t a = alpha[*col];
            if (a == 0) {
                continue;  // the four pixels keep what they hold
            }
            uint16_t c = palette[*col], *p = out + 2 * y;
            // each pixel over its own background: row 2k + 1 is no copy of row 2k, unlike fx_fire_draw's memcpy
            p[0] = alpha_blend_565(c, p[0], a);
            p[1] = alpha_blend_565(c, p[1], a);
            p[pw] = alpha_blend_565(c, p[pw], a);
            p[pw + 1] = alpha_blend_565(c, p[pw + 1], a);
        }
    }
}

// --- MVP (P608) ---
void fx_fire_set_filter(fx_fire_t *f, int32_t filter) {
    f->filter = filter;
}

// --- MVP (P596) ---
void fx_fire_set_base(fx_fire_t *f, int32_t on) {
    f->base = on ? 1 : 0;
}

void fx_fire_burn(fx_fire_t *f, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r, uint8_t heat) {
    int32_t w = f->w, h = f->h, dx = x1 - x0, dy = y1 - y0;
    int32_t n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    for (int32_t i = 0; i <= n; i++) {
        int32_t cx = x0 + (n ? dx * i / n : 0), cy = y0 + (n ? dy * i / n : 0);
        for (int32_t oy = -r; oy <= r; oy++) {
            int32_t y = cy + oy;
            if (y < 0 || y >= h) {
                continue;
            }
            uint8_t *row = f->heat + y * w;
            for (int32_t ox = -r; ox <= r; ox++) {
                int32_t x = cx + ox;
                if (x >= 0 && x < w && ox * ox + oy * oy <= r * r && row[x] < heat) {
                    row[x] = heat;
                }
            }
        }
    }
}

// --- Later, not MVP: kept for other games, the PO 2026-11-04 (P602) ---
void fx_fire_scroll(fx_fire_t *f, int32_t dx) {
    int32_t w = f->w, h = f->h, n = dx < 0 ? -dx : dx;
    if (n >= w) {
        memset(f->heat, 0, (size_t)w * (size_t)h);
    } else if (n > 0) {
        for (int32_t y = 0; y < h; y++) {
            uint8_t *row = f->heat + y * w;
            if (dx < 0) {  // left: the cells at the right edge are uncovered
                memmove(row, row + n, (size_t)(w - n));
                memset(row + w - n, 0, (size_t)n);
            } else {
                memmove(row + n, row, (size_t)(w - n));
                memset(row, 0, (size_t)n);
            }
        }
    }
    for (int32_t i = 0; i < FX_FIRE_MAX_EMBERS; i++) {  // an ember moved out of the field dies at the next step
        if (f->ember_heat[i] != 0) {
            f->ember_x[i] += dx * 256;
        }
    }
}

// --- MVP (P747) ---
int32_t fx_fire_take_births(fx_fire_t *f, int8_t *rises, int32_t n) {
    int32_t births = f->born;
    for (int32_t i = 0; rises != NULL && i < n; i++) {  // P751
        rises[i] = i < births && i < FX_FIRE_MAX_RISES ? (int8_t)f->born_rise[i] : 0;
    }
    f->born = 0;
    return births;
}

// --- MVP (P753) ---
// Total: 4 cells per 32-bit word. The mask 0x00FF00FF takes cells 0 and 2 of the word into two 16-bit lanes, and the
// shifted word takes cells 1 and 3. A word adds at most 510 to a lane, so after 128 words (65,280) the lanes are folded
// into the total, before they could carry into each other.
static uint32_t fold(uint32_t lanes) {
    return (lanes & 0xFFFF) + (lanes >> 16);
}

static int32_t heat_total(const uint8_t *heat, int32_t cells) {
    uint32_t total = 0, lanes = 0;
    int32_t words = ((uintptr_t)heat & 3) == 0 ? cells >> 2 : 0, k = 0;  // the field's own heap block is aligned
    const uint32_t *word = (const uint32_t *)heat;
    int32_t i = 0;
    for (; i + 4 <= words; i += 4) {  // 16 cells a turn: a memcpy here was a call per word (1.5 ms a call)
        uint32_t a = word[i], b = word[i + 1], c = word[i + 2], d = word[i + 3];
        lanes += (a & 0x00FF00FFu) + ((a >> 8) & 0x00FF00FFu) + (b & 0x00FF00FFu) + ((b >> 8) & 0x00FF00FFu);
        lanes += (c & 0x00FF00FFu) + ((c >> 8) & 0x00FF00FFu) + (d & 0x00FF00FFu) + ((d >> 8) & 0x00FF00FFu);
        if ((k += 4) == 128) {  // 128 words put at most 128 x 510 = 65280 in a 16-bit lane
            total += fold(lanes);
            lanes = 0;
            k = 0;
        }
    }
    for (; i < words; i++) {
        lanes += (word[i] & 0x00FF00FFu) + ((word[i] >> 8) & 0x00FF00FFu);
    }
    total += fold(lanes);
    for (i = 4 * words; i < cells; i++) {
        total += heat[i];
    }
    return (int32_t)total;
}

void fx_fire_stats(const fx_fire_t *f, int32_t threshold, int32_t *total, int32_t *height) {
    int32_t w = f->w, h = f->h;
    *total = heat_total(f->heat, w * h);
    *height = 0;
    for (int32_t row = 0; row < h; row++) {
        const uint8_t *cells = f->heat + (size_t)row * (size_t)w;
        int32_t hot = 0;
        for (int32_t x = 0; x < w && hot < 4; x++) {
            hot += cells[x] >= threshold;
        }
        if (hot >= 4) {
            *height = h - row;
            return;
        }
    }
}
