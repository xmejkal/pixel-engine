// The fire's noise (P753), see fx_noise.h. --- MVP (P753) ---
#include "fx_noise.h"

#define GLIDE 9  // a value moves 1/2^9 of the way to its target a sample: a time constant of 512 samples, 32 ms
#define PEAK 32767

// xorshift16 (shifts 7, 9, 8): 1 to 65535, every value once in 65535 draws. The noise is its draw, centred, 14 bits.
static int32_t noise(fx_noise_t *s) {
    int32_t x = s->rng;
    x ^= (x << 7) & 0xFFFF;
    x ^= x >> 9;
    x ^= (x << 8) & 0xFFFF;
    s->rng = x;
    return (x - 32768) >> 2;
}

static int32_t clamp(int32_t v, int32_t hi) {
    return v < 0 ? 0 : v > hi ? hi : v;
}

void fx_noise_init(fx_noise_t *s, uint32_t seed) {
    int32_t *p = (int32_t *)s;
    for (uint32_t i = 0; i < sizeof(fx_noise_t) / sizeof(int32_t); i++) {
        p[i] = 0;
    }
    s->rng = (int32_t)(seed & 0xFFFF) ? (int32_t)(seed & 0xFFFF) : 1;
}

// --- Later, not MVP: the bed is off in FireSound (BED = False), kept for a wind sound that follows the gusts ---
void fx_noise_bed(fx_noise_t *s, int32_t level, int32_t cutoff_coef) {
    s->bed_level_to = clamp(level, FX_NOISE_MAX_LEVEL) << 8;
    s->bed_coef_to = clamp(cutoff_coef, FX_NOISE_MAX_BED_COEF) << 8;
}

void fx_noise_whoosh(fx_noise_t *s, int32_t i, int32_t level, int32_t centre_coef) {
    s->level_to[i] = clamp(level, FX_NOISE_MAX_LEVEL) << 8;
    s->coef_to[i] = clamp(centre_coef, FX_NOISE_MAX_WHOOSH_COEF) << 8;
}

void fx_noise_mix(fx_noise_t *s, int16_t *buf, int32_t n) {
    for (int32_t k = 0; k < n; k++) {
        int32_t in = buf[k], sum = in;
        if (s->bed_level != 0 || s->bed_level_to != 0) {
            s->bed_level += (s->bed_level_to - s->bed_level) >> GLIDE;
            s->bed_coef += (s->bed_coef_to - s->bed_coef) >> GLIDE;
            int32_t c = s->bed_coef >> 8, x = noise(s);
            s->y1 += (c * (x - s->y1)) >> 15;
            s->y2 += (c * (s->y1 - s->y2)) >> 15;
            sum += (s->y2 * (s->bed_level >> 8)) >> 11;
        }
        for (int32_t i = 0; i < FX_NOISE_VOICES; i++) {
            if (s->level[i] == 0 && s->level_to[i] == 0) {
                continue;
            }
            s->level[i] += (s->level_to[i] - s->level[i]) >> GLIDE;
            s->coef[i] += (s->coef_to[i] - s->coef[i]) >> GLIDE;
            int32_t f = s->coef[i] >> 8, x = noise(s);
            int32_t high = x - s->low[i] - s->band[i];
            s->band[i] += (f * high) >> 15;
            s->low[i] += (f * s->band[i]) >> 15;
            sum += (s->band[i] * (s->level[i] >> 8)) >> 14;
        }
        if (sum != in) {  // only a sum that something changed is clamped
            buf[k] = (int16_t)(sum > PEAK ? PEAK : sum < -PEAK ? -PEAK : sum);
        }
    }
}
