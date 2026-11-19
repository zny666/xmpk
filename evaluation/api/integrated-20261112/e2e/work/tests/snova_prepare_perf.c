#include "xmpk_asm.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#define DIMENSION 5
#define COLS 5
#define BATCH 5
#define APPLICATIONS 24
#define RIGHT_STRIDE (DIMENSION * COLS)
#define RESULT_STRIDE (DIMENSION * COLS)

static uint8_t left[APPLICATIONS * DIMENSION * DIMENSION]
    __attribute__((aligned(4096)));
static uint8_t right[BATCH * RIGHT_STRIDE]
    __attribute__((aligned(4096)));
static uint8_t output[APPLICATIONS * BATCH * RESULT_STRIDE]
    __attribute__((aligned(4096)));
static uint8_t reference[APPLICATIONS * BATCH * RESULT_STRIDE]
    __attribute__((aligned(4096)));

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

static void scalar_reference(void) {
  memset(reference, 0, sizeof(reference));
  for (unsigned application = 0; application < APPLICATIONS; ++application)
    for (unsigned item = 0; item < BATCH; ++item)
      for (unsigned row = 0; row < DIMENSION; ++row)
        for (unsigned col = 0; col < COLS; ++col)
          for (unsigned inner = 0; inner < DIMENSION; ++inner)
            reference[(application * BATCH + item) * RESULT_STRIDE +
                      row * COLS + col] ^=
                gf16_mul(
                    left[(application * DIMENSION + row) * DIMENSION + inner],
                    right[item * RIGHT_STRIDE + inner * COLS + col]);
}

int main(void) {
  for (unsigned i = 0; i < sizeof(left); ++i)
    left[i] = (uint8_t)((7 * i + 3) & 0xf);
  for (unsigned i = 0; i < sizeof(right); ++i)
    right[i] = (uint8_t)((11 * i + 5) & 0xf);

  uint64_t start = read_cycle();
#if PERF_PREPARED
  if (!xmpk_gf16_linxfm_prepare_right_batch(
          right, RIGHT_STRIDE, DIMENSION, COLS, BATCH))
    tohost_exit(0x3fffffff);
#endif
  for (unsigned application = 0; application < APPLICATIONS; ++application) {
    uint8_t *destination =
        output + application * BATCH * RESULT_STRIDE;
#if PERF_PREPARED
    if (!xmpk_gf16_linxfm_apply_prepared(
            left + application * DIMENSION * DIMENSION,
            destination, RESULT_STRIDE, 0))
      tohost_exit(0x3fffffff);
#else
    if (!xmpk_gf16_linxfm_batch_same_left(
            left + application * DIMENSION * DIMENSION,
            right, RIGHT_STRIDE, destination, RESULT_STRIDE,
            DIMENSION, COLS, BATCH, 0))
      tohost_exit(0x3fffffff);
#endif
  }
  uint64_t cycles = read_cycle() - start;

  scalar_reference();
  if (memcmp(output, reference, sizeof(output)) != 0)
    tohost_exit(0x3fffffff);
  tohost_exit((cycles & UINT64_C(0x3fffffff)) + 1);
}
