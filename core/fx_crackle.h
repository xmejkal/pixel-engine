// The fire's crackle (P747): a soft roar and one pop per ember, rendered into 16 kHz int16 mono samples. Plain C99
// with no libc call and no MicroPython or IDF header, so the Mac test builds it with clang, and the board loads it as
// the native module crackle_c (natmod/crackle) with no firmware rebuild. games/crackle.py's Crackle is the same
// sound in plain Python, the naive path and the reference: a Mac test wants the two equal sample for sample.
// --- MVP (P747) ---
//
// The rules that keep the C and the Python equal, and the Python free of heap use on the board: every state and every
// intermediate value stays under 2^30 (a MicroPython small int), so the noise is xorshift16, a draw (under 2^16)
// multiplies a range under 2^14, and every other product has one factor under 2^14 and one under 2^16; signed values
// shift only with >> (arithmetic in clang and GCC, floor in Python); no / or %; the sum is clamped before it is stored.
#ifndef FX_CRACKLE_H
#define FX_CRACKLE_H

#include <stdint.h>

#define FX_CRACKLE_SLOTS 8  // pops that sound at once; a pop with no free slot counts in dropped
#define FX_CRACKLE_PEAK 32767

// The caller owns the state, one per crackle (a bytearray on the board). No static state.
typedef struct {
    int32_t rng;  // xorshift16 state, 1 to 65535
    int32_t roar1, roar2;  // the roar's two one-pole low-passes over the noise
    // the look, see fx_crackle_init
    int32_t roar_shift, roar_gain, pop_min, pop_max, decay_min, decay_max, tails, tail_gap, gain;
    int32_t popped, started, dropped;  // popped has one writer (add_pops), started and dropped another (render)
    int32_t wait[FX_CRACKLE_SLOTS];  // samples until the slot's burst starts; a slot is free when wait and env are 0
    int32_t armed[FX_CRACKLE_SLOTS];  // the level the next burst starts at
    int32_t env[FX_CRACKLE_SLOTS];  // the burst's level now, under 2^15
    int32_t decay[FX_CRACKLE_SLOTS];  // the env's factor a sample, in 1/32768
    int32_t lp[FX_CRACKLE_SLOTS];  // the burst's one-pole low-pass over the noise
    int32_t shift[FX_CRACKLE_SLOTS];  // that low-pass's shift, 1 to 3: 1 is bright, 3 is dull
    int32_t tails_left[FX_CRACKLE_SLOTS];  // small clicks still to come after the burst
} fx_crackle_t;

// Zero the state, seed the noise (seed & 0xFFFF, 0 becomes 1) and set the look, each number checked by the caller.
// roar_shift 1 to 8: the roar's low-passes, y += (x - y) >> roar_shift, so 4 is about 160 Hz. roar_gain 0 to 255: the
// roar's level in 1/256, 0 silent. pop_min <= pop_max, under 16384: a burst's level, drawn between them. decay_min <=
// decay_max, under 32768 and at most 16383 apart: the burst's factor a sample in 1/32768, drawn between them; at
// 16 kHz, 28400 lasts about 3 ms and 31600 about 12 ms to a thousandth. tails 0 to 3: at most this many small clicks
// after a burst, each at half the level before it. tail_gap 0 to 16383: a click comes 32 + a draw under tail_gap
// samples after the sound before it. gain 0 to 1023: the whole sum in 1/64, so 64 keeps it, before the clamp.
void fx_crackle_init(fx_crackle_t *c, uint32_t seed, int32_t roar_shift, int32_t roar_gain, int32_t pop_min,
                     int32_t pop_max, int32_t decay_min, int32_t decay_max, int32_t tails, int32_t tail_gap,
                     int32_t gain);

// n more pops, n >= 0. They start in the next render. Only this adds to popped.
void fx_crackle_add_pops(fx_crackle_t *c, int32_t n);

// n samples into buf. First each waiting pop (popped - started - dropped) takes the lowest free slot, with its level,
// decay, low-pass, tails and a start 1 to 128 samples into this render drawn in that order, or counts in dropped. Then
// each sample draws the noise, steps the roar and each slot in order, and stores the clamped sum.
void fx_crackle_render(fx_crackle_t *c, int16_t *buf, int32_t n);

#endif
