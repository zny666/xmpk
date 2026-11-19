#include "xmpk_asm.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#define N 128
#define ITEMS 3
#define REPEATS 1

static uint8_t matrices[ITEMS * N * N] __attribute__((aligned(4096)));
static uint8_t vector[N] __attribute__((aligned(4096)));
static uint8_t output[ITEMS * N] __attribute__((aligned(4096)));

static inline uint64_t read_cycle(void) {
  uint64_t value;
  __asm__ volatile("rdcycle %0" : "=r"(value) :: "memory");
  return value;
}

static uint8_t gf256_mul_bits(uint8_t a, uint8_t b) {
  uint8_t result = 0;
  for (unsigned i = 0; i < 8; ++i) {
    uint8_t high = a >> 7;
    result ^= (uint8_t)(-(b & 1u) & a);
    a = (uint8_t)((a << 1) ^ (0x1b & -high));
    b >>= 1;
  }
  return result;
}

static int sampled_result_is_correct(void) {
  const unsigned sampled_rows[] = {0, 1, N / 2, N - 1};
  for (unsigned item = 0; item < ITEMS; ++item)
    for (unsigned r = 0; r < sizeof(sampled_rows) / sizeof(sampled_rows[0]); ++r) {
      unsigned row = sampled_rows[r];
      uint8_t expected = 0;
      for (unsigned col = 0; col <= row; ++col)
        expected ^= gf256_mul_bits(
            matrices[(item * N + row) * N + col], vector[col]);
      if (output[item * N + row] != expected)
        return 0;
    }
  return 1;
}

int main(void) {
  for (unsigned i = 0; i < sizeof(matrices); ++i)
    matrices[i] = (uint8_t)(13 * i + 7);
  for (unsigned i = 0; i < sizeof(vector); ++i)
    vector[i] = (uint8_t)(31 * i + 9);

  uint64_t start = read_cycle();
  for (unsigned repeat = 0; repeat < REPEATS; ++repeat)
    for (unsigned item = 0; item < ITEMS; item += PERF_BATCH) {
      unsigned batch = ITEMS - item;
      if (batch > PERF_BATCH)
        batch = PERF_BATCH;
      if (!xmpk_gf256_matvec_batch(
              matrices + item * N * N, N * N, vector,
              output + item * N, N, N, 2, batch))
        tohost_exit(0x3fffffff);
    }
  uint64_t cycles = read_cycle() - start;

  if (!sampled_result_is_correct())
    tohost_exit(0x3fffffff);
  tohost_exit((cycles & UINT64_C(0x3fffffff)) + 1);
}
