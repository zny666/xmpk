#include "xmpk_asm.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#define ROWS 52
#define INNER 52
#define DEGREE 3
#define REPEATS 16

static uint8_t matrix[ROWS * INNER * DEGREE]
    __attribute__((aligned(4096)));
static uint8_t vector[INNER * DEGREE] __attribute__((aligned(4096)));
static uint8_t output[ROWS * DEGREE] __attribute__((aligned(4096)));
static uint8_t reference[ROWS * DEGREE] __attribute__((aligned(4096)));
enum { CHUNKS = (INNER + 7) / 8 };
static uint64_t prepacked_matrix[ROWS][CHUNKS][DEGREE]
    __attribute__((aligned(4096)));
static uint64_t prepacked_vector[CHUNKS][DEGREE]
    __attribute__((aligned(4096)));

static inline uint64_t read_cycle(void) {
  uint64_t value;
  __asm__ volatile("rdcycle %0" : "=r"(value) :: "memory");
  return value;
}

static inline uint8_t reduce127(uint32_t value) {
  return (uint8_t)(value % 127u);
}

static void scalar_matvec(uint8_t *dst) {
  for (unsigned row = 0; row < ROWS; ++row) {
    uint32_t acc[5] = {0};
    for (unsigned k = 0; k < INNER; ++k) {
      const uint8_t *a = &matrix[(row * INNER + k) * DEGREE];
      const uint8_t *b = &vector[k * DEGREE];
      for (unsigned i = 0; i < DEGREE; ++i)
        for (unsigned j = 0; j < DEGREE; ++j)
          acc[i + j] += (uint32_t)a[i] * b[j];
    }
    for (int t = 4; t >= 3; --t) {
      acc[t - 3] += acc[t];
      acc[t - 2] += acc[t];
    }
    for (unsigned i = 0; i < DEGREE; ++i)
      dst[row * DEGREE + i] = reduce127(acc[i]);
  }
}

static inline uint64_t pack_left(const uint8_t *a) {
  return (uint64_t)a[0] * UINT64_C(0x0000000000010101) |
         (uint64_t)a[1] * UINT64_C(0x0000010101000000) |
         (uint64_t)a[2] * UINT64_C(0x0101000000000000);
}

static inline uint64_t pack_right(const uint8_t *b) {
  return (uint64_t)b[0] * UINT64_C(0x0001000001000001) |
         (uint64_t)b[1] * UINT64_C(0x0100000100000100) |
         (uint64_t)b[2] * UINT64_C(0x0000010000010000);
}

static void mul8_matvec(uint8_t *dst) {
  uint64_t packed_vector[INNER];
  for (unsigned k = 0; k < INNER; ++k)
    packed_vector[k] = pack_right(&vector[k * DEGREE]);

  for (unsigned row = 0; row < ROWS; ++row) {
    uint32_t acc[5] = {0};
    for (unsigned k = 0; k < INNER; ++k) {
      const uint8_t *a = &matrix[(row * INNER + k) * DEGREE];
      const uint8_t *b = &vector[k * DEGREE];
      uint64_t p = xmpk_gf127_mul8(pack_left(a), packed_vector[k]);
      acc[0] += (p >> 0) & 0xff;
      acc[1] += ((p >> 8) & 0xff) + ((p >> 24) & 0xff);
      acc[2] += ((p >> 16) & 0xff) + ((p >> 32) & 0xff) +
                ((p >> 48) & 0xff);
      acc[3] += ((p >> 40) & 0xff) + ((p >> 56) & 0xff);
      acc[4] += (uint32_t)a[2] * b[2];
    }
    for (int t = 4; t >= 3; --t) {
      acc[t - 3] += acc[t];
      acc[t - 2] += acc[t];
    }
    for (unsigned i = 0; i < DEGREE; ++i)
      dst[row * DEGREE + i] = reduce127(acc[i]);
  }
}

static inline uint32_t sum_bytes(uint64_t value) {
  uint64_t pair = (value & UINT64_C(0x00ff00ff00ff00ff)) +
                  ((value >> 8) & UINT64_C(0x00ff00ff00ff00ff));
  uint64_t quad = (pair & UINT64_C(0x0000ffff0000ffff)) +
                  ((pair >> 16) & UINT64_C(0x0000ffff0000ffff));
  return (uint32_t)(quad & UINT64_C(0xffffffff)) +
         (uint32_t)(quad >> 32);
}

static void striped_mul8_matvec(uint8_t *dst) {
  uint64_t packed_vector[CHUNKS][DEGREE];
  for (unsigned chunk = 0; chunk < CHUNKS; ++chunk) {
    for (unsigned coefficient = 0; coefficient < DEGREE; ++coefficient) {
      uint64_t packed = 0;
      for (unsigned lane = 0; lane < 8; ++lane) {
        unsigned k = chunk * 8 + lane;
        if (k < INNER)
          packed |= (uint64_t)vector[k * DEGREE + coefficient] <<
                    (8 * lane);
      }
      packed_vector[chunk][coefficient] = packed;
    }
  }

  for (unsigned row = 0; row < ROWS; ++row) {
    uint32_t acc[5] = {0};
    for (unsigned chunk = 0; chunk < CHUNKS; ++chunk) {
      uint64_t packed_matrix[DEGREE] = {0};
      for (unsigned coefficient = 0; coefficient < DEGREE; ++coefficient)
        for (unsigned lane = 0; lane < 8; ++lane) {
          unsigned k = chunk * 8 + lane;
          if (k < INNER)
            packed_matrix[coefficient] |=
                (uint64_t)matrix[(row * INNER + k) * DEGREE + coefficient]
                << (8 * lane);
        }
      for (unsigned i = 0; i < DEGREE; ++i)
        for (unsigned j = 0; j < DEGREE; ++j)
          acc[i + j] += sum_bytes(xmpk_gf127_mul8(
              packed_matrix[i], packed_vector[chunk][j]));
    }
    for (int t = 4; t >= 3; --t) {
      acc[t - 3] += acc[t];
      acc[t - 2] += acc[t];
    }
    for (unsigned i = 0; i < DEGREE; ++i)
      dst[row * DEGREE + i] = reduce127(acc[i]);
  }
}

static void prepare_striped_layout(void) {
  memset(prepacked_matrix, 0, sizeof(prepacked_matrix));
  memset(prepacked_vector, 0, sizeof(prepacked_vector));
  for (unsigned chunk = 0; chunk < CHUNKS; ++chunk)
    for (unsigned lane = 0; lane < 8; ++lane) {
      unsigned k = chunk * 8 + lane;
      if (k >= INNER)
        continue;
      for (unsigned coefficient = 0; coefficient < DEGREE; ++coefficient) {
        prepacked_vector[chunk][coefficient] |=
            (uint64_t)vector[k * DEGREE + coefficient] << (8 * lane);
        for (unsigned row = 0; row < ROWS; ++row)
          prepacked_matrix[row][chunk][coefficient] |=
              (uint64_t)matrix[(row * INNER + k) * DEGREE + coefficient]
              << (8 * lane);
      }
    }
}

static void prepacked_mul8_matvec(uint8_t *dst) {
  for (unsigned row = 0; row < ROWS; ++row) {
    uint32_t acc[5] = {0};
    for (unsigned chunk = 0; chunk < CHUNKS; ++chunk)
      for (unsigned i = 0; i < DEGREE; ++i)
        for (unsigned j = 0; j < DEGREE; ++j)
          acc[i + j] += sum_bytes(xmpk_gf127_mul8(
              prepacked_matrix[row][chunk][i],
              prepacked_vector[chunk][j]));
    for (int t = 4; t >= 3; --t) {
      acc[t - 3] += acc[t];
      acc[t - 2] += acc[t];
    }
    for (unsigned i = 0; i < DEGREE; ++i)
      dst[row * DEGREE + i] = reduce127(acc[i]);
  }
}

int main(void) {
  for (unsigned i = 0; i < sizeof(matrix); ++i)
    matrix[i] = (uint8_t)((17u * i + 3u) % 127u);
  for (unsigned i = 0; i < sizeof(vector); ++i)
    vector[i] = (uint8_t)((29u * i + 5u) % 127u);
  prepare_striped_layout();

  uint64_t start = read_cycle();
  for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
#if PERF_MODE == 3
    prepacked_mul8_matvec(output);
#elif PERF_MODE == 2
    striped_mul8_matvec(output);
#elif PERF_MODE == 1
    mul8_matvec(output);
#else
    scalar_matvec(output);
#endif
  }
  uint64_t cycles = read_cycle() - start;

  scalar_matvec(reference);
  if (memcmp(output, reference, sizeof(output)) != 0)
    tohost_exit(0x3fffffff);
  tohost_exit((cycles & UINT64_C(0x3fffffff)) + 1);
}
