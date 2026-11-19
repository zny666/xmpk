#ifndef XMPK_E2E_ASM_H
#define XMPK_E2E_ASM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint64_t lo;
  uint64_t hi;
} __attribute__((aligned(16))) xmpk_gf127_slot;

static inline void xmpk_qgemm_setup(const uint64_t *a, const uint64_t *b,
                                    uint64_t *c) {
  register uint64_t x asm("a0") = (uint64_t)a;
  register uint64_t y asm("a1") = (uint64_t)b;
  register uint64_t z asm("a2") = (uint64_t)c;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x6c<<25)|(11<<20)|(10<<15)|(1<<12)|(12<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(z)
      : "r"(x), "r"(y)
      : "memory");
}

static inline uint64_t xmpk_qgemm_run(unsigned m, unsigned k,
                                      unsigned nwords, unsigned mode) {
  register uint64_t cfg asm("a0") =
      (uint64_t)m | ((uint64_t)k << 8) |
      ((uint64_t)nwords << 16) | ((uint64_t)mode << 28);
  register uint64_t unused asm("a1") = 0;
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

static inline void xmpk_solveq_setup(const uint64_t *augmented,
                                     uint64_t *result) {
  register uint64_t a asm("a0") = (uint64_t)augmented;
  register uint64_t b asm("a1") = (uint64_t)result;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x04<<25)|(11<<20)|(10<<15)|(2<<12)|(0<<7)|0x7b\n\t"
      ".balign 4"
      :
      : "r"(a), "r"(b)
      : "memory");
}

static inline uint64_t xmpk_solveq_run(unsigned n) {
  register uint64_t value asm("a0") = n;
  register uint64_t unused asm("a1") = 0;
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

static inline uint64_t xmpk_gf127_mul8(uint64_t lhs, uint64_t rhs) {
  register uint64_t a asm("a0") = lhs;
  register uint64_t b asm("a1") = rhs;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x0c<<25)|(11<<20)|(10<<15)|(1<<12)|(10<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(a)
      : "r"(b)
      : "memory");
  return a;
}

static inline uint64_t xmpk_gf256_mul8(uint64_t lhs, uint64_t rhs) {
  register uint64_t a asm("a0") = lhs;
  register uint64_t b asm("a1") = rhs;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x08<<25)|(11<<20)|(10<<15)|(1<<12)|(10<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(a)
      : "r"(b)
      : "memory");
  return a;
}

static inline uint64_t xmpk_gf256_inv8(uint64_t value) {
  register uint64_t a asm("a0") = value;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x20<<25)|(10<<15)|(1<<12)|(10<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(a)
      :
      : "memory");
  return a;
}

static inline uint64_t xmpk_gf16_macc_word(uint64_t acc, uint64_t packed,
                                           uint8_t scalar) {
  register uint64_t a asm("a0") = packed;
  register uint64_t b asm("a1") =
      (uint64_t)(scalar & 0xf) * UINT64_C(0x1111111111111111);
  register uint64_t c asm("a2") = acc;
  asm volatile(
      ".balign 4\n\t"
      ".word (1<<25)|(11<<20)|(10<<15)|(1<<12)|(12<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(c)
      : "r"(a), "r"(b)
      : "memory");
  return c;
}

static inline void xmpk_linxfm_setup(const uint64_t *transform,
                                     const uint64_t *input,
                                     uint64_t *output) {
  register uint64_t a asm("a0") = (uint64_t)transform;
  register uint64_t b asm("a1") = (uint64_t)input;
  register uint64_t c asm("a2") = (uint64_t)output;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x0c<<25)|(11<<20)|(10<<15)|(2<<12)|(12<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(c)
      : "r"(a), "r"(b)
      : "memory");
}

static inline uint64_t xmpk_linxfm_run(unsigned dimension,
                                       unsigned count) {
  register uint64_t cfg asm("a0") =
      (uint64_t)dimension | ((uint64_t)count << 8);
  register uint64_t unused asm("a1") = 0;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x10<<25)|(11<<20)|(10<<15)|(2<<12)|(10<<7)|0x7b\n\t"
      "fence rw,rw\n\t"
      ".balign 4"
      : "+r"(cfg)
      : "r"(unused)
      : "memory");
  return cfg;
}

static inline void xmpk_extgemm_setup(const void *a, const void *b,
                                      void *c) {
  register uint64_t x asm("a0") = (uint64_t)a;
  register uint64_t y asm("a1") = (uint64_t)b;
  register uint64_t z asm("a2") = (uint64_t)c;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x14<<25)|(11<<20)|(10<<15)|(2<<12)|(12<<7)|0x7b\n\t"
      ".balign 4"
      : "+r"(z)
      : "r"(x), "r"(y)
      : "memory");
}

static inline uint64_t xmpk_extgemm_run(unsigned m, unsigned k,
                                        unsigned n, unsigned degree) {
  register uint64_t cfg asm("a0") =
      (uint64_t)m | ((uint64_t)k << 8) | ((uint64_t)n << 16) |
      (degree == 10 ? UINT64_C(0x1000000) : 0);
  register uint64_t unused asm("a1") = 0;
  asm volatile(
      ".balign 4\n\t"
      ".word (0x18<<25)|(11<<20)|(10<<15)|(2<<12)|(10<<7)|0x7b\n\t"
      "fence rw,rw\n\t"
      ".balign 4"
      : "+r"(cfg)
      : "r"(unused)
      : "memory");
  return cfg;
}

int xmpk_gf16_solve_colmajor(uint8_t *matrix, uint8_t *constant,
                             unsigned n);
int xmpk_gf256_matvec(const uint8_t *matrix, const uint8_t *vector,
                      uint8_t *result, unsigned n, unsigned matrix_type);
int xmpk_gf256_matvec_batch(const uint8_t *matrices,
                            size_t matrix_stride, const uint8_t *vector,
                            uint8_t *results, size_t result_stride,
                            unsigned n, unsigned matrix_type,
                            unsigned batch);
int xmpk_gf256_matprod(const uint8_t *vectors, size_t vector_bytes,
                       unsigned count, const uint8_t *scalars,
                       uint8_t *result);
int xmpk_gf256_matprod_batch(
    const uint8_t *vectors, size_t vector_bytes, unsigned count,
    const uint8_t *scalars, size_t scalar_stride, uint8_t *results,
    size_t result_stride, unsigned rows);
int xmpk_gf16_matmul_bytes(const uint8_t *left, const uint8_t *right,
                           uint8_t *result, unsigned rows, unsigned inner,
                           unsigned cols, int add);
int xmpk_gf16_mvec_dot_add(const uint8_t *scalars,
                           size_t scalar_stride,
                           const uint64_t *vectors,
                           size_t vector_stride_words, uint64_t *acc,
                           unsigned count, unsigned words);
int xmpk_gf16_batch_matvec_add(const uint8_t *vectors,
                               size_t vector_stride,
                               unsigned vector_nibble_offset,
                               const uint8_t *matrix, uint8_t *results,
                               unsigned batch, unsigned inner,
                               unsigned output_bytes);
int xmpk_gf16_packed_gemm_add(
    const uint8_t *scalars, size_t scalar_row_stride,
    size_t scalar_inner_stride, const uint64_t *vectors,
    size_t vector_row_stride_words, uint64_t *acc,
    size_t acc_row_stride_words, unsigned rows, unsigned inner,
    unsigned words);
int xmpk_gf16_upper_transpose_matmul(
    const uint8_t *left_columns, size_t left_column_stride,
    const uint8_t *right, uint8_t *upper, unsigned inner,
    unsigned dimension, unsigned coefficient_bytes);
int xmpk_gf16_linxfm_matmul_add(const uint8_t *left,
                                const uint8_t *right, uint8_t *result,
                                unsigned dimension, unsigned cols, int add);
int xmpk_gf16_linxfm_batch_same_left(
    const uint8_t *left, const uint8_t *right, size_t right_stride,
    uint8_t *result, size_t result_stride, unsigned dimension,
    unsigned cols, unsigned batch, int add);
int xmpk_gf16_linxfm_prepare_right_batch(
    const uint8_t *right, size_t right_stride, unsigned dimension,
    unsigned cols, unsigned batch);
int xmpk_gf16_linxfm_apply_prepared(
    const uint8_t *left, uint8_t *result, size_t result_stride, int add);
int xmpk_gf127_extgemm(const uint8_t *a, const uint8_t *b, uint8_t *c,
                       unsigned m, unsigned k, unsigned n,
                       unsigned degree, int transpose_b);
int xmpk_gf127_extgemm_prepare_a(const uint8_t *a, unsigned m,
                                 unsigned k, unsigned degree);
int xmpk_gf127_extgemm_run_prepared_a(const uint8_t *b, uint8_t *c,
                                      unsigned n, int transpose_b);
int xmpk_gf127_extgemm_run_prepared_a_packed(
    const xmpk_gf127_slot *b, xmpk_gf127_slot *c, unsigned n);
int xmpk_gf127_extgemm_run_prepared_a_packed_rows(
    const xmpk_gf127_slot *b, xmpk_gf127_slot *c, unsigned row_offset,
    unsigned rows, unsigned n);
/* Short-test workspace cleanup; tracking is enabled by
   QRUOV_KEYGEN_SHORT_TEST, without adding bookkeeping to production builds. */
void xmpk_gf127_extgemm_scrub_workspace(void);
void xmpk_gf127_extgemm_clear_prepared_a(void);

#endif
