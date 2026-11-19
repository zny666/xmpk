#include "xmpk_asm.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#define M 35
#define K 102
#define L 3
#define REPEATS 105

static uint8_t matrix[M * K * L] __attribute__((aligned(4096)));
static uint8_t vector[K * L] __attribute__((aligned(4096)));
static uint8_t output[M * L] __attribute__((aligned(4096)));
static uint8_t reference[M * L] __attribute__((aligned(4096)));

static inline uint64_t read_cycle(void) {
  uint64_t value;
  __asm__ volatile("rdcycle %0" : "=r"(value) :: "memory");
  return value;
}

static void multiply(uint8_t result[L], const uint8_t a[L],
                     const uint8_t b[L]) {
  uint32_t accum[2 * L - 1] = {0};
  for (unsigned i = 0; i < L; ++i)
    for (unsigned j = 0; j < L; ++j)
      accum[i + j] += (uint32_t)a[i] * b[j];
  for (int i = 2 * L - 2; i >= L; --i) {
    accum[i - L] += accum[i];
    accum[i - L + 1] += accum[i];
  }
  for (unsigned i = 0; i < L; ++i)
    result[i] = (uint8_t)(accum[i] % 127);
}

static void scalar_reference(void) {
  uint8_t product[L];
  memset(reference, 0, sizeof(reference));
  for (unsigned row = 0; row < M; ++row)
    for (unsigned inner = 0; inner < K; ++inner) {
      multiply(product, &matrix[(row * K + inner) * L],
               &vector[inner * L]);
      for (unsigned lane = 0; lane < L; ++lane)
        reference[row * L + lane] =
            (uint8_t)((reference[row * L + lane] + product[lane]) % 127);
    }
}

int main(void) {
  for (unsigned i = 0; i < sizeof(matrix); ++i)
    matrix[i] = (uint8_t)((17 * i + 3) % 127);
  for (unsigned i = 0; i < sizeof(vector); ++i)
    vector[i] = (uint8_t)((29 * i + 5) % 127);

  uint64_t start = read_cycle();
#if PERF_PREPARED
  if (!xmpk_gf127_extgemm_prepare_a(matrix, M, K, L))
    tohost_exit(0x3fffffff);
  for (unsigned i = 0; i < REPEATS; ++i)
    if (!xmpk_gf127_extgemm_run_prepared_a(vector, output, 1, 0))
      tohost_exit(0x3fffffff);
  xmpk_gf127_extgemm_clear_prepared_a();
#else
  for (unsigned i = 0; i < REPEATS; ++i)
    if (!xmpk_gf127_extgemm(matrix, vector, output, M, K, 1, L, 0))
      tohost_exit(0x3fffffff);
#endif
  uint64_t cycles = read_cycle() - start;

  scalar_reference();
  if (memcmp(output, reference, sizeof(output)) != 0)
    tohost_exit(0x3fffffff);
  tohost_exit((cycles & UINT64_C(0x3fffffff)) + 1);
}
