// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
/* P6-1 autonomous rectangular GF(16) QGEMM regression. */
#include <stdint.h>

typedef unsigned long long u64;
extern void __attribute__((noreturn)) tohost_exit(unsigned long code);

#define MAX_M 16
#define MAX_K 64
#define MAX_NWORDS 256
#define MAX_AWORDS ((MAX_K + 15) / 16)

static u64 A[MAX_M * MAX_AWORDS] __attribute__((aligned(4096)));
static u64 B[MAX_K * MAX_NWORDS] __attribute__((aligned(4096)));
static u64 C[MAX_M * MAX_NWORDS] __attribute__((aligned(4096)));
static u64 R[MAX_M * MAX_NWORDS] __attribute__((aligned(4096)));
static volatile u64 cycle_results[8];

static inline void qgemm_setup(const u64 *a, const u64 *b, u64 *c) {
  register u64 x asm("a0") = (u64)a;
  register u64 y asm("a1") = (u64)b;
  register u64 z asm("a2") = (u64)c;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x6c<<25)|(11<<20)|(10<<15)|(1<<12)|(12<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(z)
      : "r"(x), "r"(y)
      : "memory");
}

static inline u64 qgemm_run(unsigned m, unsigned k, unsigned nwords,
                            unsigned mode) {
  register u64 cfg asm("a0") =
      (u64)m | ((u64)k << 8) | ((u64)nwords << 16) |
      ((u64)mode << 28);
  register u64 unused asm("a1") = 0;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x70<<25)|(11<<20)|(10<<15)|(1<<12)|(10<<7)|0x7b\n\t"
      "fence rw,rw\n\t"
      ".balign 4"
      : "+r"(cfg)
      : "r"(unused)
      : "memory");
  return cfg;
}

static inline u64 rdcycle(void) {
  u64 value;
  asm volatile("rdcycle %0" : "=r"(value) : : "memory");
  return value;
}

static uint8_t gf16_mul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  for (int bit = 0; bit < 4; ++bit) {
    r ^= (uint8_t)(-(b & 1) & a);
    b >>= 1;
    a = (uint8_t)(((a << 1) & 0xf) ^ ((a >> 3) * 3));
  }
  return r & 0xf;
}

static uint8_t gf256_mul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  for (int bit = 0; bit < 8; ++bit) {
    r ^= (uint8_t)(-(b & 1) & a);
    b >>= 1;
    a = (uint8_t)((a << 1) ^ ((a >> 7) * 0x1b));
  }
  return r;
}

static unsigned lanes_for_mode(unsigned mode) {
  return mode == 0 ? 16 : 8;
}

static unsigned modulus_for_mode(unsigned mode) {
  return mode == 2 ? 7 : mode == 3 ? 31 : 127;
}

static uint8_t field_mul(uint8_t a, uint8_t b, unsigned mode) {
  if (mode == 0)
    return gf16_mul(a, b);
  if (mode == 1)
    return gf256_mul(a, b);
  return (uint8_t)(((unsigned)a * b) % modulus_for_mode(mode));
}

static uint8_t field_add(uint8_t a, uint8_t b, unsigned mode) {
  if (mode <= 1)
    return a ^ b;
  return (uint8_t)(((unsigned)a + b) % modulus_for_mode(mode));
}

static uint8_t get_element(const u64 *matrix, unsigned row_words,
                           unsigned row, unsigned col, unsigned mode) {
  unsigned lanes = lanes_for_mode(mode);
  unsigned shift = (mode == 0 ? 4 : 8) * (col % lanes);
  unsigned mask = mode == 0 ? 0xf : 0xff;
  return (matrix[row * row_words + col / lanes] >> shift) & mask;
}

static void set_element(u64 *matrix, unsigned row_words, unsigned row,
                        unsigned col, uint8_t value, unsigned mode) {
  unsigned lanes = lanes_for_mode(mode);
  unsigned width = mode == 0 ? 4 : 8;
  unsigned shift = width * (col % lanes);
  u64 mask = ((1ULL << width) - 1) << shift;
  u64 *word = &matrix[row * row_words + col / lanes];
  *word = (*word & ~mask) | ((u64)value << shift);
}

static uint8_t get_nibble(const u64 *matrix, unsigned row_words,
                          unsigned row, unsigned col) {
  return (matrix[row * row_words + (col >> 4)] >> (4 * (col & 15))) &
         0xf;
}

static void set_nibble(u64 *matrix, unsigned row_words, unsigned row,
                       unsigned col, uint8_t value) {
  u64 *word = &matrix[row * row_words + (col >> 4)];
  unsigned shift = 4 * (col & 15);
  *word = (*word & ~(0xfULL << shift)) | ((u64)value << shift);
}

static u64 rng_state = 0x123456789abcdef0ULL;
static u64 rnd(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

static void init_case(unsigned m, unsigned k, unsigned nwords) {
  unsigned awords = (k + 15) / 16;
  for (unsigned i = 0; i < m * awords; ++i)
    A[i] = rnd();
  for (unsigned i = 0; i < k * nwords; ++i)
    B[i] = rnd();
  for (unsigned i = 0; i < m * nwords; ++i)
    C[i] = R[i] = 0;
}

static void reference(unsigned m, unsigned k, unsigned nwords) {
  unsigned awords = (k + 15) / 16;
  unsigned n = 16 * nwords;
  for (unsigned i = 0; i < m; ++i)
    for (unsigned kk = 0; kk < k; ++kk)
      for (unsigned j = 0; j < n; ++j)
        set_nibble(R, nwords, i, j,
                   get_nibble(R, nwords, i, j) ^
                       gf16_mul(get_nibble(A, awords, i, kk),
                                get_nibble(B, nwords, kk, j)));
}

static void run_case(unsigned tag, unsigned m, unsigned k, unsigned nwords) {
  init_case(m, k, nwords);
  qgemm_setup(A, B, C);
  u64 start = rdcycle();
  u64 count = qgemm_run(m, k, nwords, 0);
  cycle_results[2 * tag] = rdcycle() - start;
  if (count != (u64)m * k * nwords * 16)
    tohost_exit(820 + tag);

  start = rdcycle();
  reference(m, k, nwords);
  cycle_results[2 * tag + 1] = rdcycle() - start;
  for (unsigned i = 0; i < m * nwords; ++i)
    if (C[i] != R[i])
      tohost_exit(830 + 16 * tag + i);
}

static void run_field_case(unsigned tag, unsigned mode, unsigned m,
                           unsigned k, unsigned nwords) {
  unsigned lanes = lanes_for_mode(mode);
  unsigned awords = (k + lanes - 1) / lanes;
  unsigned n = lanes * nwords;
  unsigned modulus = mode >= 2 ? modulus_for_mode(mode) : 256;
  for (unsigned index = 0; index < m * awords; ++index)
    A[index] = 0;
  for (unsigned index = 0; index < k * nwords; ++index)
    B[index] = 0;
  for (unsigned index = 0; index < m * nwords; ++index)
    C[index] = R[index] = 0;
  for (unsigned row = 0; row < m; ++row)
    for (unsigned kk = 0; kk < k; ++kk)
      set_element(A, awords, row, kk,
                  mode >= 2 ? rnd() % modulus : rnd(), mode);
  for (unsigned kk = 0; kk < k; ++kk)
    for (unsigned col = 0; col < n; ++col)
      set_element(B, nwords, kk, col,
                  mode >= 2 ? rnd() % modulus : rnd(), mode);
  for (unsigned row = 0; row < m; ++row)
    for (unsigned kk = 0; kk < k; ++kk)
      for (unsigned col = 0; col < n; ++col)
        set_element(
            R, nwords, row, col,
            field_add(get_element(R, nwords, row, col, mode),
                      field_mul(get_element(A, awords, row, kk, mode),
                                get_element(B, nwords, kk, col, mode),
                                mode),
                      mode),
            mode);
  qgemm_setup(A, B, C);
  if (qgemm_run(m, k, nwords, mode) != (u64)m * k * nwords * lanes)
    tohost_exit(860 + tag);
  for (unsigned index = 0; index < m * nwords; ++index)
    if (C[index] != R[index])
      tohost_exit(870 + 16 * tag + index);
}

int main(void) {
#ifdef PERF_ONLY
  init_case(PERF_M, PERF_K, PERF_NWORDS);
#ifdef PERF_SCALAR
  u64 start = rdcycle();
  reference(PERF_M, PERF_K, PERF_NWORDS);
  u64 scalar_cycles = rdcycle() - start;
  tohost_exit((scalar_cycles & 0x3fffffff) + 1);
#else
  qgemm_setup(A, B, C);
  u64 start = rdcycle();
  u64 count = qgemm_run(PERF_M, PERF_K, PERF_NWORDS, 0);
  u64 hw_cycles = rdcycle() - start;
  if (count != (u64)PERF_M * PERF_K * PERF_NWORDS * 16)
    tohost_exit(890);
  tohost_exit((hw_cycles & 0x3fffffff) + 1);
#endif
#else
  run_case(0, 3, 5, 1);
  run_case(1, 7, 19, 2);
  run_case(2, 16, 64, 2);
  run_case(3, 1, 1, 256);
  run_field_case(0, 1, 3, 9, 2);
  run_field_case(1, 2, 2, 7, 1);
  run_field_case(2, 3, 2, 7, 1);
  run_field_case(3, 4, 2, 7, 1);

  qgemm_setup(A, B, C);
  if (!(qgemm_run(0, 16, 1, 0) >> 63))
    tohost_exit(880);
  qgemm_setup((const u64 *)((u64)A + 1), B, C);
  if (!(qgemm_run(1, 16, 1, 0) >> 63))
    tohost_exit(881);
  qgemm_setup(A, B, C);
  if (!(qgemm_run(1, 1, 1, 5) >> 63))
    tohost_exit(882);
  if (!(qgemm_run(1, 1, 4096, 0) >> 63))
    tohost_exit(883);
  return 0;
#endif
}
