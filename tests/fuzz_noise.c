// A fuzz run of core/fx_noise.c and fx_fire_stats (P753) under AddressSanitizer and UBSan, which stop at the first
// signed overflow, out-of-bounds read or bad shift. Random targets (in range and far out of it), random input buffers
// at the clamp, and random fields of any width. Build and run from the repository root:
//   clang -std=c99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror \
//     -Icore -Idriver/src tests/fuzz_noise.c core/fx_noise.c core/fx_fire.c -o /tmp/fuzz_noise && /tmp/fuzz_noise
// --- MVP (P753) ---
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx_fire.h"
#include "fx_noise.h"

static uint32_t seed = 753;

static uint32_t rnd(void) {  // xorshift32, only for this program
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static int32_t any(int32_t lo, int32_t hi) {  // lo to hi, both ends reachable
    return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1));
}

static int32_t pick(int32_t in_range_hi) {  // mostly in range, sometimes the ends, sometimes far out
    switch (rnd() % 6) {
    case 0: return 0;
    case 1: return in_range_hi;
    case 2: return -any(0, 1 << 30);
    case 3: return any(0, 1 << 30);
    default: return any(0, in_range_hi);
    }
}

static void fuzz_noise(int rounds) {
    static fx_noise_t s;
    static int16_t buf[300];
    fx_noise_init(&s, rnd());
    for (int r = 0; r < rounds; r++) {
        int what = (int)(rnd() % 8);
        if (what == 0) {
            fx_noise_bed(&s, pick(FX_NOISE_MAX_LEVEL), pick(FX_NOISE_MAX_BED_COEF));
        } else if (what < 4) {
            fx_noise_whoosh(&s, any(0, FX_NOISE_VOICES - 1), pick(FX_NOISE_MAX_LEVEL), pick(FX_NOISE_MAX_WHOOSH_COEF));
        } else if (what == 4) {
            fx_noise_init(&s, rnd());
        }
        int32_t n = any(1, 300);
        for (int32_t k = 0; k < n; k++) {
            buf[k] = rnd() % 3 == 0 ? (int16_t)(rnd() % 2 ? 32767 : -32768) : (int16_t)any(-32768, 32767);
        }
        fx_noise_mix(&s, buf, n);
    }
}

static void fuzz_stats(int rounds) {
    for (int r = 0; r < rounds; r++) {
        int32_t w = any(1, 70), h = any(1, 40), threshold = any(0, 255);
        uint8_t *heat = malloc((size_t)w * (size_t)h), *cooling = calloc(FX_FIRE_MAP_W * FX_FIRE_MAP_H, 1);
        fx_fire_t f;
        fx_fire_init(&f, heat, cooling, w, h, rnd());
        int kind = (int)(rnd() % 3);
        for (int32_t i = 0; i < w * h; i++) {
            heat[i] = kind == 0 ? 255 : kind == 1 ? (rnd() % 20 == 0 ? (uint8_t)rnd() : 0) : (uint8_t)rnd();
        }
        int32_t total, height, want_total = 0, want_height = 0;
        fx_fire_stats(&f, threshold, &total, &height);
        for (int32_t i = 0; i < w * h; i++) {
            want_total += heat[i];
        }
        for (int32_t row = 0; row < h && !want_height; row++) {
            int32_t hot = 0;
            for (int32_t x = 0; x < w; x++) {
                hot += heat[row * w + x] >= threshold;
            }
            want_height = hot >= 4 ? h - row : 0;
        }
        if (total != want_total || height != want_height) {
            printf("stats differ: w %d h %d threshold %d total %d/%d height %d/%d\n", w, h, threshold, total,
                   want_total, height, want_height);
            abort();
        }
        free(heat);
        free(cooling);
    }
}

int main(void) {
    fuzz_noise(200000);
    fuzz_stats(20000);
    // the largest field the binding takes, all cells at 255: the total must not overflow
    int32_t w = 1024, h = 1024, total, height;
    uint8_t *heat = malloc((size_t)w * (size_t)h), *cooling = calloc(FX_FIRE_MAP_W * FX_FIRE_MAP_H, 1);
    fx_fire_t f;
    fx_fire_init(&f, heat, cooling, w, h, 1);
    memset(heat, 255, (size_t)w * (size_t)h);
    fx_fire_stats(&f, 255, &total, &height);
    if (total != 255 * w * h || height != h) {
        printf("full field: total %d height %d\n", total, height);
        return 1;
    }
    free(heat);
    free(cooling);
    printf("fuzz ok\n");
    return 0;
}
