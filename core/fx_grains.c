// The fire's grains (P751), see fx_grains.h. --- MVP (P751) ---
#include "fx_grains.h"

// xorshift16 (shifts 7, 9, 8): 1 to 65535, every value once in 65535 draws.
static int32_t draw(fx_grains_t *g) {
    int32_t x = g->rng;
    x ^= (x << 7) & 0xFFFF;
    x ^= x >> 9;
    x ^= (x << 8) & 0xFFFF;
    g->rng = x;
    return x;
}

// lo + a draw times (hi - lo) in 1/65536: lo to hi - 1. hi - lo under 2^14.
static int32_t between(fx_grains_t *g, int32_t lo, int32_t hi) {
    return lo + ((draw(g) * (hi - lo)) >> 16);
}

int32_t fx_grains_init(fx_grains_t *g, uint32_t seed, const int32_t *look, const int32_t *index, int32_t index_ints) {
    if (look[0] < 0 || look[0] > 32768 || look[1] < 0 || look[1] > 32768) {
        return 1;
    }
    if (index_ints < 4 || index_ints > FX_GRAINS_INDEX_INTS || (index_ints - 1) % 3 != 0) {
        return 1;
    }
    int32_t n_pops = (index_ints - 1) / 3, bed_len = index[0], seen = 0, end = bed_len;
    if (bed_len < FX_GRAINS_MIN_BED || bed_len > 131071) {
        return 1;
    }
    for (int32_t i = 0; i < n_pops; i++) {
        int32_t off = index[1 + 3 * i], len = index[2 + 3 * i], size = index[3 + 3 * i];
        if (off < bed_len || off > 1048576 || len < 1 || len > 2400 || size < 0 || size >= FX_GRAINS_SIZES) {
            return 1;
        }
        seen |= 1 << size;
        end = off + len > end ? off + len : end;
    }
    if (seen != (1 << FX_GRAINS_SIZES) - 1) {
        return 1;
    }
    for (int32_t i = 0; i < FX_GRAINS_RISES; i++) {
        g->rise[i] = 0;
    }
    g->popped = g->started = g->dropped = g->next_in = g->lev1 = g->lev2 = 0;
    g->rng = (int32_t)(seed & 0xFFFF) ? (int32_t)(seed & 0xFFFF) : 1;
    g->bed_len = bed_len;
    g->bank_len = end;
    g->n_pops = n_pops;
    g->bed_gain = look[0];
    g->pop_gain = look[1];
    for (int32_t i = 0; i < FX_GRAINS_POPS; i++) {
        g->p_off[i] = i < n_pops ? index[1 + 3 * i] : 0;
        g->p_len[i] = i < n_pops ? index[2 + 3 * i] : 0;
        g->p_size[i] = i < n_pops ? index[3 + 3 * i] : 0;
    }
    for (int32_t i = 0; i < FX_GRAINS_BED_SLOTS; i++) {
        g->g_pos[i] = g->g_len[i] = 1;  // free
        g->g_log[i] = g->g_start[i] = g->g_gain[i] = 0;
    }
    for (int32_t i = 0; i < FX_GRAINS_POP_SLOTS; i++) {
        g->s_pos[i] = g->s_len[i] = 1;  // free
        g->s_off[i] = g->s_gain[i] = 0;
    }
    return 0;
}

void fx_grains_add_pops(fx_grains_t *g, int32_t n, const int8_t *rises) {
    for (int32_t j = 0; j < n && j < FX_GRAINS_RISES; j++) {
        g->rise[(g->popped + j) & (FX_GRAINS_RISES - 1)] = rises ? rises[j] : 0;
    }
    for (int32_t j = FX_GRAINS_RISES; j < n; j++) {
        g->rise[(g->popped + j) & (FX_GRAINS_RISES - 1)] = 0;
    }
    g->popped += n;
}

int32_t fx_grains_size_of_rise(int32_t rise, int32_t drawn) {
    return rise == 5 ? 2 : rise == 4 ? 1 : drawn;
}

int32_t fx_grains_bank_len(const fx_grains_t *g) {
    return g->bank_len;
}

// The size of a pop by the u^3 rule: u is a draw in 0 to 1, u^3 under 0.25 is small (63 % of pops), under 0.6 is
// middle (21 %), else big (16 %).
static int32_t draw_size(fx_grains_t *g) {
    int32_t u = draw(g);
    int32_t u2 = ((u >> 1) * (u >> 1)) >> 14;  // u^2 under 2^16, from a product under 2^30
    int32_t u3 = ((u2 >> 1) * (u >> 1)) >> 14;  // u^3 under 2^16
    return u3 < 16384 ? 0 : u3 < 39322 ? 1 : 2;
}

// The waiting pops into free slots, or into dropped.
static void start_pops(fx_grains_t *g) {
    while (g->started + g->dropped < g->popped) {
        int32_t i = 0;
        while (i < FX_GRAINS_POP_SLOTS && g->s_pos[i] < g->s_len[i]) {
            i++;
        }
        if (i == FX_GRAINS_POP_SLOTS) {
            g->dropped++;
            continue;
        }
        int32_t size = fx_grains_size_of_rise(g->rise[(g->started + g->dropped) & (FX_GRAINS_RISES - 1)], draw_size(g));
        int32_t same = 0, pick, j;
        for (j = 0; j < g->n_pops; j++) {
            same += g->p_size[j] == size;
        }
        pick = between(g, 0, same);
        for (j = 0; j < g->n_pops; j++) {
            if (g->p_size[j] == size && pick-- == 0) {
                break;
            }
        }
        g->s_off[i] = g->p_off[j];
        g->s_len[i] = g->p_len[j];
        g->s_pos[i] = 0;
        g->s_gain[i] = (between(g, 19661, 32768) * g->pop_gain) >> 15;  // 0.6 to 1.0, then the look
        g->started++;
    }
}

// The next grain in a free slot, if there is one: 64 to 256 ms from a random start in the bed. The one after it starts
// FADE samples before this one ends, so the fade-out and the next fade-in are the same 32 ms and the bed never dips.
static void start_grain(fx_grains_t *g) {
    int32_t i = 0;
    while (i < FX_GRAINS_BED_SLOTS && g->g_pos[i] < g->g_len[i]) {
        i++;
    }
    if (i == FX_GRAINS_BED_SLOTS) {
        return;  // try again next sample
    }
    g->g_log[i] = between(g, 10, 13);
    g->g_len[i] = 1 << g->g_log[i];
    g->g_start[i] = (draw(g) * ((g->bed_len - g->g_len[i]) >> 4)) >> 12;  // 0 to bed_len - g_len
    g->g_gain[i] = between(g, 24576, 32768);
    g->g_pos[i] = 0;
    g->next_in = g->g_len[i] - FX_GRAINS_FADE;
}

void fx_grains_render(fx_grains_t *g, const int16_t *bank, int16_t *buf, int32_t n) {
    start_pops(g);
    for (int32_t k = 0; k < n; k++) {
        // the bed's level: noise (under 2^20 in 1/16) through two poles at 2^-11, so it moves at about 1 Hz
        g->lev1 += ((draw(g) - 32768) * 16 - g->lev1) >> 11;
        g->lev2 += (g->lev1 - g->lev2) >> 11;
        int32_t level = 26000 + g->lev2;  // Q15, 19661 to 32768 after the clamp
        level = level < 19661 ? 19661 : level > 32768 ? 32768 : level;
        if (g->next_in > 0) {
            g->next_in--;
        }
        if (g->next_in == 0) {
            start_grain(g);
        }
        int32_t bed = 0;
        for (int32_t i = 0; i < FX_GRAINS_BED_SLOTS; i++) {
            if (g->g_pos[i] >= g->g_len[i]) {
                continue;
            }
            int32_t pos = g->g_pos[i]++, left = g->g_len[i] - pos;
            int32_t ramp = pos < FX_GRAINS_FADE ? pos : left < FX_GRAINS_FADE ? left : FX_GRAINS_FADE;
            int32_t window = ramp << 6;  // 0 to 32768: a ramp in, flat, a ramp out
            int32_t v = (bank[g->g_start[i] + pos] * window) >> 15;
            bed += (v * g->g_gain[i]) >> 15;
        }
        bed = (((bed >> 2) * ((level * g->bed_gain) >> 15)) >> 13);  // bed under 2^17, so under 2^30 after the product
        int32_t sum = bed;
        for (int32_t i = 0; i < FX_GRAINS_POP_SLOTS; i++) {
            if (g->s_pos[i] >= g->s_len[i]) {
                continue;
            }
            sum += (bank[g->s_off[i] + g->s_pos[i]++] * g->s_gain[i]) >> 15;
        }
        buf[k] = (int16_t)(sum > FX_GRAINS_PEAK ? FX_GRAINS_PEAK : sum < -FX_GRAINS_PEAK ? -FX_GRAINS_PEAK : sum);
    }
}
