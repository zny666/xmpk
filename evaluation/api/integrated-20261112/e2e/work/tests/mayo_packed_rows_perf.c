#include "xmpk_asm.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#ifndef PERF_ROWS
#define PERF_ROWS 12
#endif
#define ROWS PERF_ROWS
#define INNER 142
#define WORDS 108
#define REPEATS 1

static uint8_t scalars[ROWS * INNER] __attribute__((aligned(4096)));
static uint64_t vectors[INNER * WORDS] __attribute__((aligned(4096)));
static uint64_t output[ROWS * WORDS] __attribute__((aligned(4096)));

static inline uint64_t read_cycle(void) {
  uint64_t value;
  __asm__ volatile("rdcycle %0" : "=r"(value) :: "memory");
  return value;
}

static uint8_t gf16_mul(uint8_t a, uint8_t b) {
  uint8_t result = 0;
  for (unsigned i = 0; i < 4; ++i) {
    uint8_t high = a >> 3;
    result ^= (uint8_t)(-(b & 1u) & a);
    a = (uint8_t)(((a << 1) & 0xf) ^ (3 & -high));
    b >>= 1;
  }
  return result & 0xf;
}

static uint8_t nibble_get(const uint64_t *words, unsigned index) {
  return (uint8_t)((words[index / 16] >> (4 * (index % 16))) & 0xf);
}

static int sampled_result_is_correct(void) {
  const unsigned sampled_rows[] = {0, ROWS - 1};
  const unsigned sampled_words[] = {0, WORDS - 1};
  for (unsigned r = 0; r < sizeof(sampled_rows) / sizeof(sampled_rows[0]); ++r)
    for (unsigned w = 0;
         w < sizeof(sampled_words) / sizeof(sampled_words[0]); ++w)
      for (unsigned lane = 0; lane < 16; ++lane) {
        unsigned row = sampled_rows[r];
        unsigned word = sampled_words[w];
        uint8_t expected = 0;
        for (unsigned inner = 0; inner < INNER; ++inner)
          expected ^= gf16_mul(
              scalars[row * INNER + inner],
              nibble_get(vectors + inner * WORDS, word * 16 + lane));
        if (nibble_get(output + row * WORDS, word * 16 + lane) != expected)
          return 0;
      }
  return 1;
}

int main(void) {
  for (unsigned i = 0; i < sizeof(scalars); ++i)
    scalars[i] = (uint8_t)((13 * i + 1) & 0xf);
  for (unsigned i = 0; i < INNER * WORDS; ++i) {
    uint64_t value = 0;
    for (unsigned lane = 0; lane < 16; ++lane)
      value |= (uint64_t)((17 * (i * 16 + lane) + 5) & 0xf)
               << (4 * lane);
    vectors[i] = value;
  }

  uint64_t start = read_cycle();
  for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
    memset(output, 0, sizeof(output));
#if PERF_BATCH_ROWS
    if (!xmpk_gf16_packed_gemm_add(
            scalars, INNER, 1, vectors, WORDS, output, WORDS,
            ROWS, INNER, WORDS))
      tohost_exit(0x3fffffff);
#else
    for (unsigned row = 0; row < ROWS; ++row)
      if (!xmpk_gf16_packed_gemm_add(
              scalars + row * INNER, 0, 1, vectors, WORDS,
              output + row * WORDS, WORDS, 1, INNER, WORDS))
        tohost_exit(0x3fffffff);
#endif
  }
  uint64_t cycles = read_cycle() - start;

  if (!sampled_result_is_correct())
    tohost_exit(0x3fffffff);
  tohost_exit((cycles & UINT64_C(0x3fffffff)) + 1);
}
