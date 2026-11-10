// The fire's crackle (P747), see fx_crackle.h. --- MVP (P747) ---
#include "fx_crackle.h"

// xorshift16 (shifts 7, 9, 8): 1 to 65535, every value once in 65535 draws.
static int32_t draw(fx_crackle_t *c) {
    int32_t x = c->rng;
    x ^= (x << 7) & 0xFFFF;
    x ^= x >> 9;
    x ^= (x << 8) & 0xFFFF;
    c->rng = x;
    return x;
}

// lo + a draw times (hi - lo) in 1/65536, so lo to hi - 1, or lo when hi <= lo.
static int32_t between(fx_crackle_t *c, int32_t lo, int32_t hi) {
    return lo + ((draw(c) * (hi - lo)) >> 16);
}

void fx_crackle_init(fx_crackle_t *c, uint32_t seed, int32_t roar_shift, int32_t roar_gain, int32_t pop_min,
                     int32_t pop_max, int32_t decay_min, int32_t decay_max, int32_t tails, int32_t tail_gap,
                     int32_t gain) {
    c->rng = (int32_t)(seed & 0xFFFF) ? (int32_t)(seed & 0xFFFF) : 1;
    c->roar1 = c->roar2 = 0;
    c->roar_shift = roar_shift;
    c->roar_gain = roar_gain;
    c->pop_min = pop_min;
    c->pop_max = pop_max;
    c->decay_min = decay_min;
    c->decay_max = decay_max;
    c->tails = tails;
    c->tail_gap = tail_gap;
    c->gain = gain;
    c->popped = c->started = c->dropped = 0;
    for (int32_t i = 0; i < FX_CRACKLE_SLOTS; i++) {
        c->wait[i] = c->armed[i] = c->env[i] = c->decay[i] = c->lp[i] = c->shift[i] = c->tails_left[i] = 0;
    }
}

void fx_crackle_add_pops(fx_crackle_t *c, int32_t n) {
    c->popped += n;
}

// The waiting pops into free slots, or into dropped.
static void start_pops(fx_crackle_t *c) {
    while (c->started + c->dropped < c->popped) {
        int32_t i = 0;
        while (i < FX_CRACKLE_SLOTS && (c->wait[i] || c->env[i])) {
            i++;
        }
        if (i == FX_CRACKLE_SLOTS) {
            c->dropped++;
            continue;
        }
        c->armed[i] = between(c, c->pop_min, c->pop_max);
        c->decay[i] = between(c, c->decay_min, c->decay_max);
        c->shift[i] = between(c, 1, 4);
        c->tails_left[i] = between(c, 0, c->tails + 1);
        c->wait[i] = 1 + (draw(c) & 127);
        c->lp[i] = 0;
        c->started++;
    }
}

void fx_crackle_render(fx_crackle_t *c, int16_t *buf, int32_t n) {
    start_pops(c);
    for (int32_t k = 0; k < n; k++) {
        int32_t noise = draw(c) - 32768;
        c->roar1 += (noise - c->roar1) >> c->roar_shift;
        c->roar2 += (c->roar1 - c->roar2) >> c->roar_shift;
        int32_t sum = (c->roar2 * c->roar_gain) >> 8;
        for (int32_t i = 0; i < FX_CRACKLE_SLOTS; i++) {
            if (c->wait[i]) {
                if (--c->wait[i] == 0) {
                    c->env[i] = c->armed[i];
                }
                continue;
            }
            if (c->env[i] == 0) {
                continue;
            }
            c->lp[i] += (noise - c->lp[i]) >> c->shift[i];
            sum += (c->lp[i] * c->env[i]) >> 15;
            c->env[i] = (c->env[i] * c->decay[i]) >> 15;
            if (c->env[i] < 8) {
                c->env[i] = 0;
                if (c->tails_left[i]) {
                    c->tails_left[i]--;
                    c->armed[i] >>= 1;
                    c->wait[i] = 32 + between(c, 0, c->tail_gap);
                }
            }
        }
        sum = (sum * c->gain) >> 6;  // |sum| under 2^17 before, so under 2^27 after
        buf[k] = (int16_t)(sum > FX_CRACKLE_PEAK ? FX_CRACKLE_PEAK : sum < -FX_CRACKLE_PEAK ? -FX_CRACKLE_PEAK : sum);
    }
}
