// The fire's grains (P751): a bed of overlapping grains cut from a sample bank, and one bank pop per ember, mixed
// into 16 kHz int16 mono samples. Plain C99 with no libc call and no MicroPython or IDF header, so the Mac test builds
// it with clang, and the board loads it as the native module crackle_c (natmod/crackle). There is no filter, so
// nothing can ring or overflow. The bank is made on the Mac by pixel-games' tools/fire_bank.py and passed to every
// render: int16 samples, the bed first, then the pops. The state keeps no pointer, so the caller's bytearrays can move.
// --- MVP (P751) ---
//
// Every value is bounded, so int32 never overflows: samples under 2^15, gains and windows in Q15 (32768 is 1.0), a
// product has one factor under 2^15 and one at most 2^15, and the bed length is under 2^17 samples. No / or %: a
// grain's length is 2^10, 2^11 or 2^12 samples (64, 128 or 256 ms), so its triangle window is a shift.
#ifndef FX_GRAINS_H
#define FX_GRAINS_H

#include <stdint.h>

#define FX_GRAINS_BED_SLOTS 3  // grains that sound at once
#define FX_GRAINS_POP_SLOTS 8  // pops that sound at once; a pop with no free slot counts in dropped
#define FX_GRAINS_POPS 12  // bank pops at most
#define FX_GRAINS_SIZES 3  // pop sizes: 0 small to 2 big
#define FX_GRAINS_PEAK 32767
#define FX_GRAINS_INDEX_INTS (1 + 3 * FX_GRAINS_POPS)  // the index's int32 count at most
#define FX_GRAINS_LOOK_INTS 2
#define FX_GRAINS_MIN_BED 4096  // samples: the longest grain

// The caller owns the state, one per crackle (a bytearray on the board). No static state.
typedef struct {
    int32_t rng;  // xorshift16 state, 1 to 65535
    int32_t bed_len, bank_len, n_pops;  // samples in the bed, in the whole bank, pops in the bank
    int32_t bed_gain, pop_gain;  // the look, Q15
    int32_t popped, started, dropped;  // popped has one writer (add_pops), started and dropped another (render)
    int32_t next_in;  // samples until the next grain starts
    int32_t lev1, lev2;  // the bed level's two slow low-passes over the noise, in 1/16
    int32_t g_pos[FX_GRAINS_BED_SLOTS];  // samples played of the grain; a slot is free when g_pos >= g_len
    int32_t g_len[FX_GRAINS_BED_SLOTS];  // 1024, 2048 or 4096
    int32_t g_log[FX_GRAINS_BED_SLOTS];  // 10, 11 or 12: log2 of g_len
    int32_t g_start[FX_GRAINS_BED_SLOTS];  // the grain's first sample in the bed
    int32_t g_gain[FX_GRAINS_BED_SLOTS];  // Q15, 16384 to 32767
    int32_t p_off[FX_GRAINS_POPS], p_len[FX_GRAINS_POPS], p_size[FX_GRAINS_POPS];  // the bank's pops
    int32_t s_pos[FX_GRAINS_POP_SLOTS];  // samples played of the slot's pop; free when s_pos >= s_len
    int32_t s_len[FX_GRAINS_POP_SLOTS];
    int32_t s_off[FX_GRAINS_POP_SLOTS];  // the pop's first sample in the bank
    int32_t s_gain[FX_GRAINS_POP_SLOTS];  // Q15, at most 32768
} fx_grains_t;

// Zero the state, seed the noise (seed & 0xFFFF, 0 becomes 1), set the look and read the index. Returns 0, or 1 when
// an argument is out of its range and the state is not usable.
// look: bed_gain, pop_gain, each 0 to 32768 in Q15: the bed's and the pops' level, 32768 as the bank has them.
// index: bed_len (4096 to 131071), then per pop its offset, length (1 to 2400) and size (0 to 2), in samples of the
// bank, 1 to 12 pops and every size at least once; the bed is samples 0 to bed_len - 1 and a pop lies after it.
int32_t fx_grains_init(fx_grains_t *g, uint32_t seed, const int32_t *look, const int32_t *index, int32_t index_ints);

// n more pops, n >= 0. They start in the next render. Only this adds to popped.
void fx_grains_add_pops(fx_grains_t *g, int32_t n);

// The bank's length in samples, for the caller to check against what it has.
int32_t fx_grains_bank_len(const fx_grains_t *g);

// n samples into buf, from a bank of at least fx_grains_bank_len samples. First each waiting pop (popped - started -
// dropped) takes the lowest free slot: a size by the u^3 rule (a draw cubed, so small mostly), then a pop of that size,
// then a gain, drawn in that order, or counts in dropped. Then each sample steps the bed level and starts a grain when
// next_in is 0, sums the grains and the pops, and stores the clamped sum.
void fx_grains_render(fx_grains_t *g, const int16_t *bank, int16_t *buf, int32_t n);

#endif
