// The fire's noise (P753): a smooth bed and 8 whoosh voices of filtered noise, mixed into 16 kHz int16 mono samples.
// Plain C99 with no libc call and no MicroPython or IDF header, so the Mac test builds it with clang, and the board
// loads it as the native module crackle_c (natmod/crackle). The caller sets targets (a level and a filter coefficient
// for the bed, for each voice), and every sample glides the value 1/512 of the way to its target (about 32 ms). The
// bed is the fire's steady roar and follows the heat field. A whoosh is a body's air sound, its pitch and level follow
// the body's speed. "mix" adds into buf, it never queues pops.
// --- MVP (P753) ---
//
// Every value is bounded, so int32 never overflows, and there is no 64-bit maths, no / and no %:
//   noise x = (draw - 32768) >> 2 with a xorshift16 draw of 1 to 65535, so |x| <= 8192.
//   Bed: y1 += c * (x - y1) >> 15, y2 += c * (y1 - y2) >> 15, with c at most 16384 (Q15), |x - y| <= 16384, so a
//   product is at most 2^28. The output is y2 * level >> 11 with level at most 32768 and |y2| <= 8192: 2^28. At a
//   250 Hz cutoff its rms is about 0.36 * level and its peaks about 1.5 * level.
//   Whoosh (Chamberlin band-pass, damping 1, so no multiply for it): high = x - low - band; band += f * high >> 15;
//   low += f * band >> 15, with f at most 25080 (2000 Hz) and stable while f < 2 - damping, so f < 32768 in Q15. The
//   worst input (the signs of the impulse response) gives |low| <= 11.2k, |band| <= 13.5k, |high| <= 24.5k, so
//   f * high <= 6.2e8. The output is band * level >> 14 with level at most 32768: 4.4e8 before the shift.
//   Glide: levels and coefficients are kept in Q23 (the target << 8), and move (target - value) >> 9 a sample. A
//   positive step rounds down, so a value stops up to 511 (2 in the unshifted units) short of a target above it, and a
//   target of 0 is reached exactly.
// An idle source (level and target level both 0) takes no draw and no filter step, so level 0 everywhere leaves buf
// as it is.
#ifndef FX_NOISE_H
#define FX_NOISE_H

#include <stdint.h>

#define FX_NOISE_VOICES 8  // whoosh voices
#define FX_NOISE_MAX_LEVEL 32768  // a level, a gain in 1/32768 of the filter's output
#define FX_NOISE_MAX_BED_COEF 16384  // the bed's one-pole coefficient, Q15: 1 - exp(-2 pi fc / 16000) is 0.5 at 1.8 kHz
#define FX_NOISE_MAX_WHOOSH_COEF 25080  // 2 sin(pi fc / 16000) in Q15 at fc = 2000 Hz

// The caller owns the state, one per sound (a bytearray on the board). No static state.
typedef struct {
    int32_t rng;  // xorshift16 state, 1 to 65535
    int32_t bed_level, bed_level_to, bed_coef, bed_coef_to;  // Q23: the value now and its target
    int32_t y1, y2;  // the bed's two low-passes
    int32_t level[FX_NOISE_VOICES], level_to[FX_NOISE_VOICES];  // Q23
    int32_t coef[FX_NOISE_VOICES], coef_to[FX_NOISE_VOICES];  // Q23
    int32_t low[FX_NOISE_VOICES], band[FX_NOISE_VOICES];  // the voices' filter states
} fx_noise_t;

// Zero the state (every source silent) and seed the noise (seed & 0xFFFF, 0 becomes 1).
void fx_noise_init(fx_noise_t *s, uint32_t seed);

// The bed's targets: level 0 to FX_NOISE_MAX_LEVEL, cutoff_coef 0 to FX_NOISE_MAX_BED_COEF. A number out of its range
// is clamped to it.
void fx_noise_bed(fx_noise_t *s, int32_t level, int32_t cutoff_coef);

// Voice i's targets (i 0 to FX_NOISE_VOICES - 1, checked by the caller): level 0 to FX_NOISE_MAX_LEVEL, centre_coef
// 0 to FX_NOISE_MAX_WHOOSH_COEF. A number out of its range is clamped to it.
void fx_noise_whoosh(fx_noise_t *s, int32_t i, int32_t level, int32_t centre_coef);

// n more samples added into buf, each sum that the noise changed clamped to -32767 to 32767. The caller's n is not
// checked.
void fx_noise_mix(fx_noise_t *s, int16_t *buf, int32_t n);

#endif
