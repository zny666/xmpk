// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
#include <stdint.h>

typedef unsigned long long u64;
extern void __attribute__((noreturn)) tohost_exit(unsigned long code);

static inline u64 rdcycle(void) {
  u64 value;
  asm volatile("rdcycle %0" : "=r"(value) : : "memory");
  return value;
}

static uint8_t gf256_mul(uint8_t a, uint8_t b) {
  uint8_t result = 0;
  for (int bit = 0; bit < 8; ++bit) {
    result ^= (uint8_t)(-(b & 1) & a);
    b >>= 1;
    a = (uint8_t)((a << 1) ^ ((a >> 7) * 0x1b));
  }
  return result;
}

static uint8_t gf256_inv(uint8_t value) {
  uint8_t result = 1;
  uint8_t power = value;
  unsigned exponent = 254;
  while (exponent) {
    if (exponent & 1)
      result = gf256_mul(result, power);
    power = gf256_mul(power, power);
    exponent >>= 1;
  }
  return value ? result : 0;
}

#if defined(PERF_SOLVE256)

#ifndef N
#define N 45
#endif

#define ROW_WORDS ((N + 8) / 8)

static u64 packed_matrix[N * ROW_WORDS] __attribute__((aligned(4096)));
static u64 packed_solution[(N + 7) / 8] __attribute__((aligned(4096)));
static uint8_t scalar_matrix[N][N + 1] __attribute__((aligned(4096)));
static uint8_t scalar_solution[N];
static uint8_t expected[N];
static uint8_t gf256_exp[512];
static uint8_t gf256_log[256];

static inline void solveq_setup(const u64 *matrix, u64 *solution) {
  register u64 a asm("a0") = (u64)matrix;
  register u64 b asm("a1") = (u64)solution;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x04<<25)|(11<<20)|(10<<15)|(2<<12)|(0<<7)|0x7b\n\t"
      ".balign 4"
      :
      : "r"(a), "r"(b)
      : "memory");
}

static inline u64 solveq_run(unsigned n) {
  register u64 value asm("a0") = n | 0x100ULL;
  register u64 unused asm("a1") = 0;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x08<<25)|(11<<20)|(10<<15)|(2<<12)|(10<<7)|0x7b\n\t"
      "fence rw,rw\n\t"
      ".balign 4"
      : "+r"(value)
      : "r"(unused)
      : "memory");
  return value;
}

static void packed_set(unsigned row, unsigned col, uint8_t value) {
  u64 *word = &packed_matrix[row * ROW_WORDS + col / 8];
  unsigned shift = 8 * (col % 8);
  *word = (*word & ~(0xffULL << shift)) | ((u64)value << shift);
}

static uint8_t packed_solution_get(unsigned index) {
  return (packed_solution[index / 8] >> (8 * (index % 8))) & 0xff;
}

static void init_gf256_tables(void) {
  uint8_t value = 1;
  for (unsigned index = 0; index < 255; ++index) {
    gf256_exp[index] = value;
    gf256_log[value] = (uint8_t)index;
    value = gf256_mul(value, 3);
  }
  for (unsigned index = 255; index < 512; ++index)
    gf256_exp[index] = gf256_exp[index - 255];
}

static uint8_t gf256_mul_fast(uint8_t a, uint8_t b) {
  if (!a || !b)
    return 0;
  return gf256_exp[(unsigned)gf256_log[a] + gf256_log[b]];
}

static uint8_t gf256_inv_fast(uint8_t value) {
  return value ? gf256_exp[255 - gf256_log[value]] : 0;
}

static void init_solve(void) {
  init_gf256_tables();
  for (unsigned i = 0; i < N * ROW_WORDS; ++i)
    packed_matrix[i] = 0;
  for (unsigned i = 0; i < (N + 7) / 8; ++i)
    packed_solution[i] = 0;
  for (unsigned row = 0; row < N; ++row) {
    expected[row] = (uint8_t)(row * 73 + 11);
    for (unsigned col = 0; col <= N; ++col)
      scalar_matrix[row][col] = 0;
  }
  for (unsigned row = 0; row < N; ++row) {
    uint8_t rhs = 0;
    for (unsigned col = 0; col < N; ++col) {
      // A Cauchy matrix is dense and invertible because the x and y sets
      // are distinct and disjoint in GF(256).
      uint8_t value =
          gf256_inv_fast((uint8_t)(row ^ (128 + col)));
      scalar_matrix[row][col] = value;
      packed_set(row, col, value);
      rhs ^= gf256_mul_fast(value, expected[col]);
    }
    scalar_matrix[row][N] = rhs;
    packed_set(row, N, rhs);
  }
}

static int scalar_solve(void) {
  for (unsigned pivot = 0; pivot < N; ++pivot) {
    unsigned swap = pivot;
    while (swap < N && scalar_matrix[swap][pivot] == 0)
      ++swap;
    if (swap == N)
      return 0;
    if (swap != pivot)
      for (unsigned col = 0; col <= N; ++col) {
        uint8_t tmp = scalar_matrix[pivot][col];
        scalar_matrix[pivot][col] = scalar_matrix[swap][col];
        scalar_matrix[swap][col] = tmp;
      }
    uint8_t inverse = gf256_inv_fast(scalar_matrix[pivot][pivot]);
    for (unsigned col = pivot; col <= N; ++col)
      scalar_matrix[pivot][col] =
          gf256_mul_fast(scalar_matrix[pivot][col], inverse);
    for (unsigned row = pivot + 1; row < N; ++row) {
      uint8_t factor = scalar_matrix[row][pivot];
      if (factor)
        for (unsigned col = pivot; col <= N; ++col)
          scalar_matrix[row][col] ^=
              gf256_mul_fast(scalar_matrix[pivot][col], factor);
    }
  }
  for (int row = N - 1; row >= 0; --row) {
    uint8_t value = scalar_matrix[row][N];
    for (unsigned col = (unsigned)row + 1; col < N; ++col)
      value ^= gf256_mul_fast(
          scalar_matrix[row][col], scalar_solution[col]);
    scalar_solution[row] = value;
  }
  return 1;
}

int main(void) {
  init_solve();
  u64 start = rdcycle();
#ifdef PERF_SCALAR
  if (!scalar_solve())
    tohost_exit(0x3fffffff);
#else
  solveq_setup(packed_matrix, packed_solution);
  if (solveq_run(N) != N)
    tohost_exit(0x3fffffff);
#endif
  u64 cycles = rdcycle() - start;
  for (unsigned index = 0; index < N; ++index) {
#ifdef PERF_SCALAR
    uint8_t actual = scalar_solution[index];
#else
    uint8_t actual = packed_solution_get(index);
#endif
    if (actual != expected[index])
      tohost_exit(0x3fffffff);
  }
  tohost_exit((cycles & 0x3fffffffULL) + 1);
}

#elif defined(PERF_EXTGEMM)

#ifndef M
#define M 52
#endif
#ifndef K
#define K 52
#endif
#ifndef NCOL
#define NCOL 1
#endif
#ifndef L
#define L 3
#endif

typedef struct {
  u64 lo;
  u64 hi;
} __attribute__((aligned(16))) ext_slot;

static ext_slot ext_a[M * K] __attribute__((aligned(4096)));
static ext_slot ext_b[K * NCOL] __attribute__((aligned(4096)));
static ext_slot ext_c[M * NCOL] __attribute__((aligned(4096)));
static ext_slot ext_ref[M * NCOL] __attribute__((aligned(4096)));
static uint8_t gf127_product[127][127] __attribute__((aligned(4096)));
#ifdef PERF_LAYOUT
static uint8_t compact_a[M * K * L] __attribute__((aligned(4096)));
static uint8_t compact_b[K * NCOL * L] __attribute__((aligned(4096)));
static uint8_t compact_c[M * NCOL * L] __attribute__((aligned(4096)));
#endif

static uint8_t coeff_get(const ext_slot *value, unsigned index) {
  return ((const uint8_t *)value)[index];
}

static void coeff_set(ext_slot *value, unsigned index, uint8_t coeff) {
  ((uint8_t *)value)[index] = coeff;
}

static void extgemm_setup(const ext_slot *a, const ext_slot *b,
                          ext_slot *c) {
  register u64 x asm("a0") = (u64)a;
  register u64 y asm("a1") = (u64)b;
  register u64 z asm("a2") = (u64)c;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x14<<25)|(11<<20)|(10<<15)|(2<<12)|(12<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(z)
      : "r"(x), "r"(y)
      : "memory");
}

static u64 extgemm_run(void) {
  register u64 config asm("a0") =
      M | ((u64)K << 8) | ((u64)NCOL << 16) |
      (L == 10 ? 0x1000000ULL : 0);
  register u64 unused asm("a1") = 0;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x18<<25)|(11<<20)|(10<<15)|(2<<12)|(10<<7)|0x7b\n\t"
      "fence rw,rw\n\t"
      ".balign 4"
      : "+r"(config)
      : "r"(unused)
      : "memory");
  return config;
}

static void init_extgemm(void) {
  for (unsigned lhs = 0; lhs < 127; ++lhs) {
    unsigned product = 0;
    for (unsigned rhs = 0; rhs < 127; ++rhs) {
      gf127_product[lhs][rhs] = (uint8_t)product;
      product += lhs;
      if (product >= 127)
        product -= 127;
    }
  }
  for (unsigned index = 0; index < M * K; ++index) {
    ext_a[index].lo = ext_a[index].hi = 0;
    for (unsigned degree = 0; degree < L; ++degree)
#ifdef PERF_LAYOUT
      compact_a[index*L + degree] =
          (uint8_t)((index * 37 + degree * 11 + 3) % 127);
#else
      coeff_set(&ext_a[index], degree,
                (uint8_t)((index * 37 + degree * 11 + 3) % 127));
#endif
  }
  for (unsigned index = 0; index < K * NCOL; ++index) {
    ext_b[index].lo = ext_b[index].hi = 0;
    for (unsigned degree = 0; degree < L; ++degree)
#ifdef PERF_LAYOUT
      compact_b[index*L + degree] =
          (uint8_t)((index * 41 + degree * 7 + 5) % 127);
#else
      coeff_set(&ext_b[index], degree,
                (uint8_t)((index * 41 + degree * 7 + 5) % 127));
#endif
  }
  for (unsigned index = 0; index < M * NCOL; ++index) {
    ext_c[index].lo = ext_c[index].hi = 0;
    ext_ref[index].lo = ext_ref[index].hi = 0;
#ifdef PERF_LAYOUT
    for (unsigned degree = 0; degree < L; ++degree)
      compact_c[index*L + degree] = 0;
#endif
  }
}

static void scalar_extgemm(ext_slot *output) {
  const unsigned fc = L == 10 ? 2 : 1;
  const unsigned fe = L == 10 ? 3 : 1;
  for (unsigned row = 0; row < M; ++row)
    for (unsigned col = 0; col < NCOL; ++col) {
      uint32_t accum[19] = {0};
      for (unsigned kk = 0; kk < K; ++kk)
        for (unsigned ai = 0; ai < L; ++ai)
          for (unsigned bi = 0; bi < L; ++bi)
            accum[ai + bi] +=
                gf127_product[coeff_get(&ext_a[row*K + kk], ai)]
                             [coeff_get(&ext_b[kk*NCOL + col], bi)];
      for (int degree = 2*L - 2; degree >= L; --degree) {
        accum[degree-L] += accum[degree];
        accum[degree-L+fe] += fc*accum[degree];
      }
      for (unsigned degree = 0; degree < L; ++degree)
        coeff_set(&output[row*NCOL + col], degree,
                  (uint8_t)(accum[degree] % 127));
    }
}

int main(void) {
  init_extgemm();
  u64 start = rdcycle();
#ifdef PERF_SCALAR
  scalar_extgemm(ext_c);
#else
#ifdef PERF_LAYOUT
  for (unsigned index = 0; index < M * K; ++index)
    for (unsigned degree = 0; degree < L; ++degree)
      coeff_set(&ext_a[index], degree, compact_a[index*L + degree]);
  for (unsigned index = 0; index < K * NCOL; ++index)
    for (unsigned degree = 0; degree < L; ++degree)
      coeff_set(&ext_b[index], degree, compact_b[index*L + degree]);
#endif
  extgemm_setup(ext_a, ext_b, ext_c);
  if (extgemm_run() != (u64)M*K*NCOL*L*L)
    tohost_exit(0x3fffffff);
#ifdef PERF_LAYOUT
  for (unsigned index = 0; index < M * NCOL; ++index)
    for (unsigned degree = 0; degree < L; ++degree)
      compact_c[index*L + degree] = coeff_get(&ext_c[index], degree);
#endif
#endif
  u64 cycles = rdcycle() - start;
#ifndef PERF_SCALAR
  scalar_extgemm(ext_ref);
  for (unsigned index = 0; index < M * NCOL; ++index)
    if (ext_c[index].lo != ext_ref[index].lo ||
        ext_c[index].hi != ext_ref[index].hi)
      tohost_exit(0x3fffffff);
#ifdef PERF_LAYOUT
  for (unsigned index = 0; index < M * NCOL; ++index)
    for (unsigned degree = 0; degree < L; ++degree)
      if (compact_c[index*L + degree] !=
          coeff_get(&ext_ref[index], degree))
        tohost_exit(0x3fffffff);
#endif
#endif
  tohost_exit((cycles & 0x3fffffffULL) + 1);
}

#else
#error "Select PERF_SOLVE256 or PERF_EXTGEMM"
#endif
