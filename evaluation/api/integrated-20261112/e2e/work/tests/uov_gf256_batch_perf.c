#include "xmpk_asm.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#define ROWS 96
#define COUNT 163
#define VECTOR_BYTES 96
#define SCALAR_STRIDE 163
#define REPEATS 1

static uint8_t vectors[COUNT * VECTOR_BYTES]
    __attribute__((aligned(4096)));
static uint8_t scalars[ROWS * SCALAR_STRIDE]
    __attribute__((aligned(4096)));
static uint8_t output[ROWS * VECTOR_BYTES]
    __attribute__((aligned(4096)));

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
  const unsigned sampled_rows[] = {0, ROWS - 1};
  const unsigned sampled_bytes[] = {0, 1, 15, VECTOR_BYTES - 16,
                                    VECTOR_BYTES - 2, VECTOR_BYTES - 1};
  for (unsigned r = 0; r < sizeof(sampled_rows) / sizeof(sampled_rows[0]); ++r)
    for (unsigned b = 0;
         b < sizeof(sampled_bytes) / sizeof(sampled_bytes[0]); ++b) {
      unsigned row = sampled_rows[r];
      unsigned byte = sampled_bytes[b];
      uint8_t expected = 0;
    for (unsigned inner = 0; inner < COUNT; ++inner)
        expected ^= gf256_mul_bits(
            scalars[row * SCALAR_STRIDE + inner],
            vectors[inner * VECTOR_BYTES + byte]);
      if (output[row * VECTOR_BYTES + byte] != expected)
        return 0;
    }
  return 1;
}

int main(void) {
  for (unsigned i = 0; i < sizeof(vectors); ++i)
    vectors[i] = (uint8_t)(17 * i + 3);
  for (unsigned i = 0; i < sizeof(scalars); ++i)
    scalars[i] = (uint8_t)(29 * i + 5);

  uint64_t start = read_cycle();
  for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
#if PERF_BATCH
    if (!xmpk_gf256_matprod_batch(
            vectors, VECTOR_BYTES, COUNT, scalars, SCALAR_STRIDE,
            output, VECTOR_BYTES, ROWS))
      tohost_exit(0x3fffffff);
#else
    for (unsigned row = 0; row < ROWS; ++row)
      if (!xmpk_gf256_matprod(
              vectors, VECTOR_BYTES, COUNT,
              scalars + row * SCALAR_STRIDE,
              output + row * VECTOR_BYTES))
        tohost_exit(0x3fffffff);
#endif
  }
  uint64_t cycles = read_cycle() - start;

  if (!sampled_result_is_correct())
    tohost_exit(0x3fffffff);
  tohost_exit((cycles & UINT64_C(0x3fffffff)) + 1);
}
