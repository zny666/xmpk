#include "xmpk_asm.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#define MAX_ROWS 142
#define MAX_INNER 142
#define MAX_COLS 6

static uint8_t left[MAX_ROWS * MAX_INNER];
static uint8_t right[MAX_INNER * MAX_COLS];
static uint8_t output[MAX_ROWS * MAX_COLS];
static uint8_t reference[MAX_ROWS * MAX_COLS];
#define BATCH_DIM 4
#define BATCH_COUNT 4
#define BATCH_RIGHT_STRIDE 19
#define BATCH_RESULT_STRIDE 21
static uint8_t batch_right[BATCH_COUNT * BATCH_RIGHT_STRIDE];
static uint8_t batch_output[BATCH_COUNT * BATCH_RESULT_STRIDE];
static uint8_t batch_reference[BATCH_COUNT * BATCH_RESULT_STRIDE];
static uint8_t gf256_matrix[16 * 16] __attribute__((aligned(8)));
static uint8_t gf256_vector[16] __attribute__((aligned(8)));
static uint8_t gf256_output[16] __attribute__((aligned(8)));
static uint8_t gf256_reference[16];
#define GF256_BATCH 3
static uint8_t gf256_batch_matrix[GF256_BATCH * 16 * 16]
    __attribute__((aligned(8)));
static uint8_t gf256_batch_output[GF256_BATCH * 16]
    __attribute__((aligned(8)));
static uint8_t gf256_batch_reference[GF256_BATCH * 16];
#define GF256_L5_N 128
static uint8_t gf256_l5_matrix[
    GF256_BATCH * GF256_L5_N * GF256_L5_N]
    __attribute__((aligned(8)));
static uint8_t gf256_l5_vector[GF256_L5_N]
    __attribute__((aligned(8)));
static uint8_t gf256_l5_output[GF256_BATCH * GF256_L5_N]
    __attribute__((aligned(8)));
static uint8_t gf256_l5_reference[GF256_BATCH * GF256_L5_N];
#define GF256_MATPROD_WIDTH 163
#define GF256_MATPROD_BYTES (96 * 96)
static uint8_t gf256_matprod_vectors[
    GF256_MATPROD_WIDTH * GF256_MATPROD_BYTES]
    __attribute__((aligned(8)));
static uint8_t gf256_matprod_scalars[GF256_MATPROD_WIDTH];
static uint8_t gf256_matprod_output[GF256_MATPROD_BYTES]
    __attribute__((aligned(8)));
static uint8_t gf256_matprod_reference[GF256_MATPROD_BYTES];
#define GF256_MATPROD_ODD_WIDTH 96
#define GF256_MATPROD_ODD_BYTES 163
static uint8_t gf256_matprod_odd_vectors[
    GF256_MATPROD_ODD_WIDTH * GF256_MATPROD_ODD_BYTES];
static uint8_t gf256_matprod_odd_scalars[GF256_MATPROD_ODD_WIDTH];
static uint8_t gf256_matprod_odd_output[GF256_MATPROD_ODD_BYTES];
static uint8_t gf256_matprod_odd_reference[GF256_MATPROD_ODD_BYTES];
#define SOLVE_N 16
static uint8_t solve_input[SOLVE_N * (SOLVE_N / 2)];
static uint8_t solve_rhs[SOLVE_N / 2];
static uint8_t solve_expected[SOLVE_N / 2];
#define EXT_M 5
#define EXT_K 7
#define EXT_N 4
#define EXT_MAX_DEGREE 10
static uint8_t ext_a[EXT_M * EXT_K * EXT_MAX_DEGREE];
static uint8_t ext_b[EXT_N * EXT_K * EXT_MAX_DEGREE];
static uint8_t ext_output[EXT_M * EXT_N * EXT_MAX_DEGREE];
static uint8_t ext_reference[EXT_M * EXT_N * EXT_MAX_DEGREE];
#define EXT_L5_M 35
#define EXT_L5_K 102
#define EXT_L5_N 35
#define EXT_L5_DEGREE 3
static uint8_t ext_l5_a[EXT_L5_M * EXT_L5_K * EXT_L5_DEGREE];
static uint8_t ext_l5_b[EXT_L5_N * EXT_L5_K * EXT_L5_DEGREE];
static uint8_t ext_l5_output[EXT_L5_M * EXT_L5_N * EXT_L5_DEGREE];
static uint8_t ext_l5_reference[EXT_L5_M * EXT_L5_N *
                                EXT_L5_DEGREE];
#define MVEC_COUNT 23
#define MVEC_WORDS 5
#define MVEC_STRIDE 7
static uint8_t mvec_test_scalars[MVEC_COUNT * 3];
static uint64_t mvec_test_vectors[MVEC_COUNT * MVEC_STRIDE];
static uint64_t mvec_test_output[MVEC_WORDS];
static uint64_t mvec_test_reference[MVEC_WORDS];
#define PACKED_BATCH 4
#define PACKED_INNER 17
#define PACKED_OUTPUT_BYTES 16
#define PACKED_VECTOR_STRIDE 12
#define PACKED_VECTOR_OFFSET 3
static uint8_t packed_batch_vectors[PACKED_BATCH * PACKED_VECTOR_STRIDE];
static uint8_t packed_batch_matrix[PACKED_INNER * PACKED_OUTPUT_BYTES];
static uint8_t packed_batch_output[PACKED_BATCH * PACKED_OUTPUT_BYTES];
static uint8_t packed_batch_reference[PACKED_BATCH * PACKED_OUTPUT_BYTES];
#define GEMM_ROWS 3
#define GEMM_INNER 142
#define GEMM_WORDS 108
#define GEMM_VECTOR_STRIDE 110
#define GEMM_ACC_STRIDE 110
static uint8_t packed_gemm_scalars[GEMM_INNER * 4 + GEMM_ROWS];
static uint64_t packed_gemm_vectors[GEMM_INNER * GEMM_VECTOR_STRIDE];
static uint64_t packed_gemm_output[GEMM_ROWS * GEMM_ACC_STRIDE];
static uint64_t packed_gemm_reference[GEMM_ROWS * GEMM_ACC_STRIDE];
#define UPPER_INNER 17
#define UPPER_DIM 8
#define UPPER_COEFF_BYTES 8
#define UPPER_LEFT_STRIDE 10
#define UPPER_BLOCKS ((UPPER_DIM * (UPPER_DIM + 1)) / 2)
static uint8_t upper_left_columns[UPPER_DIM * UPPER_LEFT_STRIDE];
static uint8_t upper_right[
    UPPER_INNER * UPPER_DIM * UPPER_COEFF_BYTES];
static uint8_t upper_output[UPPER_BLOCKS * UPPER_COEFF_BYTES];
static uint8_t upper_reference[UPPER_BLOCKS * UPPER_COEFF_BYTES];
static uint8_t upper_full[
    UPPER_DIM * UPPER_DIM * UPPER_COEFF_BYTES];

static uint8_t gf16_mul(uint8_t a, uint8_t b) {
  uint8_t result = 0;
  for (unsigned bit = 0; bit < 4; ++bit) {
    result ^= (uint8_t)(-(b & 1) & a);
    b >>= 1;
    a = (uint8_t)(((a << 1) & 0xf) ^ ((a >> 3) * 3));
  }
  return result & 0xf;
}

static uint8_t packed_nibble_get(const uint8_t *data, unsigned index) {
  return (index & 1) ? data[index >> 1] >> 4 : data[index >> 1] & 0xf;
}

static void packed_nibble_set(uint8_t *data, unsigned index,
                              uint8_t value) {
  unsigned shift = 4 * (index & 1);
  data[index >> 1] =
      (uint8_t)((data[index >> 1] & ~(0xfu << shift)) |
                ((value & 0xfu) << shift));
}

static void run_solve_case(unsigned tag) {
  memset(solve_input, 0, sizeof(solve_input));
  memset(solve_rhs, 0, sizeof(solve_rhs));
  memset(solve_expected, 0, sizeof(solve_expected));
  for (unsigned row = 0; row < SOLVE_N; ++row) {
    uint8_t solution = (uint8_t)((5 * row + tag + 1) & 0xf);
    packed_nibble_set(solve_expected, row, solution);
    for (unsigned col = 0; col <= row; ++col) {
      uint8_t coefficient =
          col == row ? 1 : (uint8_t)((3 * row + 7 * col + tag) & 0xf);
      packed_nibble_set(solve_input + col * (SOLVE_N / 2), row,
                        coefficient);
      packed_nibble_set(
          solve_rhs, row,
          packed_nibble_get(solve_rhs, row) ^
              gf16_mul(coefficient,
                       packed_nibble_get(solve_expected, col)));
    }
  }
  if (!xmpk_gf16_solve_colmajor(solve_input, solve_rhs, SOLVE_N))
    tohost_exit(1400 + tag);
  if (memcmp(solve_rhs, solve_expected, sizeof(solve_expected)) != 0)
    tohost_exit(1500 + tag);
}

static uint8_t gf256_mul_table[256 * 256];

static uint8_t gf256_mul_raw(uint8_t a, uint8_t b) {
  uint8_t result = 0;
  for (unsigned bit = 0; bit < 8; ++bit) {
    result ^= (uint8_t)(-(b & 1) & a);
    b >>= 1;
    a = (uint8_t)((a << 1) ^ ((a >> 7) * 0x1b));
  }
  return result;
}

static void gf256_mul_table_init(void) {
  for (unsigned a = 0; a < 256; ++a)
    for (unsigned b = 0; b < 256; ++b)
      gf256_mul_table[(a << 8) | b] =
          gf256_mul_raw((uint8_t)a, (uint8_t)b);
}

static uint8_t gf256_mul(uint8_t a, uint8_t b) {
  return gf256_mul_table[((unsigned)a << 8) | b];
}

static uint8_t gf256_inv(uint8_t value) {
  if (value == 0)
    return 0;
  for (unsigned candidate = 1; candidate < 256; ++candidate)
    if (gf256_mul(value, (uint8_t)candidate) == 1)
      return (uint8_t)candidate;
  return 0;
}

static void run_gf256_packed_case(unsigned tag) {
  uint64_t lhs = 0;
  uint64_t rhs = 0;
  uint64_t expected_product = 0;
  uint64_t expected_inverse = 0;
  for (unsigned lane = 0; lane < 8; ++lane) {
    uint8_t a = (uint8_t)(17 * lane + tag + 1);
    uint8_t b = (uint8_t)(29 * lane + 3 * tag + 2);
    lhs |= (uint64_t)a << (8 * lane);
    rhs |= (uint64_t)b << (8 * lane);
    expected_product |= (uint64_t)gf256_mul(a, b) << (8 * lane);
    expected_inverse |= (uint64_t)gf256_inv(a) << (8 * lane);
  }
  if (xmpk_gf256_mul8(lhs, rhs) != expected_product)
    tohost_exit(3900 + tag);
  if (xmpk_gf256_inv8(lhs) != expected_inverse)
    tohost_exit(4000 + tag);
}

static void gf127_ext_mul(uint8_t *result, const uint8_t *a,
                          const uint8_t *b, unsigned degree) {
  uint32_t accumulator[2 * EXT_MAX_DEGREE - 1] = {0};
  const unsigned coefficient = degree == 10 ? 2 : 1;
  const unsigned exponent = degree == 10 ? 3 : 1;
  for (unsigned i = 0; i < degree; ++i)
    for (unsigned j = 0; j < degree; ++j)
      accumulator[i + j] += (uint32_t)a[i] * b[j];
  for (unsigned i = 2 * degree - 2; i >= degree; --i) {
    accumulator[i - degree] += accumulator[i];
    accumulator[i - degree + exponent] += coefficient * accumulator[i];
  }
  for (unsigned i = 0; i < degree; ++i)
    result[i] = (uint8_t)(accumulator[i] % 127);
}

static void run_extgemm_case(unsigned tag, unsigned degree,
                             int transpose_b) {
  const unsigned b_elements = transpose_b ? EXT_N * EXT_K : EXT_K * EXT_N;
  uint8_t product[EXT_MAX_DEGREE];
  for (unsigned i = 0; i < EXT_M * EXT_K * degree; ++i)
    ext_a[i] = (uint8_t)((11 * i + 3 * tag + 1) % 127);
  for (unsigned i = 0; i < b_elements * degree; ++i)
    ext_b[i] = (uint8_t)((17 * i + 5 * tag + 2) % 127);
  memset(ext_output, 0, sizeof(ext_output));
  memset(ext_reference, 0, sizeof(ext_reference));
  for (unsigned row = 0; row < EXT_M; ++row)
    for (unsigned col = 0; col < EXT_N; ++col)
      for (unsigned inner = 0; inner < EXT_K; ++inner) {
        unsigned b_index = transpose_b
                               ? col * EXT_K + inner
                               : inner * EXT_N + col;
        gf127_ext_mul(product,
                      ext_a + (row * EXT_K + inner) * degree,
                      ext_b + b_index * degree, degree);
        for (unsigned lane = 0; lane < degree; ++lane) {
          uint8_t *destination =
              ext_reference + (row * EXT_N + col) * degree + lane;
          *destination = (uint8_t)((*destination + product[lane]) % 127);
        }
      }
  if (!xmpk_gf127_extgemm(ext_a, ext_b, ext_output, EXT_M, EXT_K,
                          EXT_N, degree, transpose_b))
    tohost_exit(1200 + tag);
  if (memcmp(ext_output, ext_reference, EXT_M * EXT_N * degree) != 0)
    tohost_exit(1300 + tag);
  memset(ext_output, 0, sizeof(ext_output));
  if (!xmpk_gf127_extgemm_prepare_a(
          ext_a, EXT_M, EXT_K, degree))
    tohost_exit(1800 + tag);
  if (!xmpk_gf127_extgemm_run_prepared_a(
          ext_b, ext_output, EXT_N, transpose_b))
    tohost_exit(1900 + tag);
  if (memcmp(ext_output, ext_reference, EXT_M * EXT_N * degree) != 0)
    tohost_exit(2000 + tag);
  xmpk_gf127_extgemm_clear_prepared_a();
  if (xmpk_gf127_extgemm_run_prepared_a(
          ext_b, ext_output, EXT_N, transpose_b))
    tohost_exit(2100 + tag);
}

static void run_extgemm_l5_case(unsigned tag) {
  uint8_t product[EXT_MAX_DEGREE];

  for (unsigned i = 0; i < sizeof(ext_l5_a); ++i)
    ext_l5_a[i] = (uint8_t)((11 * i + 3 * tag + 1) % 127);
  for (unsigned i = 0; i < sizeof(ext_l5_b); ++i)
    ext_l5_b[i] = (uint8_t)((17 * i + 5 * tag + 2) % 127);
  memset(ext_l5_output, 0, sizeof(ext_l5_output));
  memset(ext_l5_reference, 0, sizeof(ext_l5_reference));
  for (unsigned row = 0; row < EXT_L5_M; ++row)
    for (unsigned col = 0; col < EXT_L5_N; ++col)
      for (unsigned inner = 0; inner < EXT_L5_K; ++inner) {
        gf127_ext_mul(
            product,
            ext_l5_a +
                (row * EXT_L5_K + inner) * EXT_L5_DEGREE,
            ext_l5_b +
                (col * EXT_L5_K + inner) * EXT_L5_DEGREE,
            EXT_L5_DEGREE);
        for (unsigned lane = 0; lane < EXT_L5_DEGREE; ++lane) {
          uint8_t *destination =
              ext_l5_reference +
              (row * EXT_L5_N + col) * EXT_L5_DEGREE + lane;
          *destination =
              (uint8_t)((*destination + product[lane]) % 127);
        }
      }
  if (!xmpk_gf127_extgemm_prepare_a(
          ext_l5_a, EXT_L5_M, EXT_L5_K, EXT_L5_DEGREE))
    tohost_exit(3000 + tag);
  if (!xmpk_gf127_extgemm_run_prepared_a(
          ext_l5_b, ext_l5_output, EXT_L5_N, 1))
    tohost_exit(3100 + tag);
  if (memcmp(ext_l5_output, ext_l5_reference,
             sizeof(ext_l5_reference)) != 0)
    tohost_exit(3200 + tag);
  xmpk_gf127_extgemm_clear_prepared_a();
}

static void run_mvec_dot_case(unsigned tag) {
  memset(mvec_test_vectors, 0, sizeof(mvec_test_vectors));
  for (unsigned i = 0; i < MVEC_COUNT; ++i) {
    mvec_test_scalars[i * 3] = (uint8_t)((7 * i + tag) & 0xf);
    for (unsigned word = 0; word < MVEC_WORDS; ++word) {
      uint64_t value = 0;
      for (unsigned lane = 0; lane < 16; ++lane)
        value |= (uint64_t)((11 * i + 5 * word + lane + tag) & 0xf)
                 << (4 * lane);
      mvec_test_vectors[i * MVEC_STRIDE + word] = value;
    }
  }
  for (unsigned word = 0; word < MVEC_WORDS; ++word) {
    mvec_test_output[word] = mvec_test_reference[word] =
        UINT64_C(0x0123456789abcdef) ^ ((uint64_t)tag << word);
  }
  for (unsigned i = 0; i < MVEC_COUNT; ++i)
    for (unsigned word = 0; word < MVEC_WORDS; ++word)
      for (unsigned lane = 0; lane < 16; ++lane) {
        uint8_t input =
            (uint8_t)(mvec_test_vectors[i * MVEC_STRIDE + word] >>
                      (4 * lane)) & 0xf;
        mvec_test_reference[word] ^=
            (uint64_t)gf16_mul(mvec_test_scalars[i * 3], input)
            << (4 * lane);
      }
  if (!xmpk_gf16_mvec_dot_add(
          mvec_test_scalars, 3, mvec_test_vectors, MVEC_STRIDE,
          mvec_test_output, MVEC_COUNT, MVEC_WORDS))
    tohost_exit(2200 + tag);
  if (memcmp(mvec_test_output, mvec_test_reference,
             sizeof(mvec_test_reference)) != 0)
    tohost_exit(2300 + tag);
}

static void run_packed_batch_matvec_case(unsigned tag) {
  memset(packed_batch_vectors, 0, sizeof(packed_batch_vectors));
  memset(packed_batch_matrix, 0, sizeof(packed_batch_matrix));
  for (unsigned row = 0; row < PACKED_BATCH; ++row)
    for (unsigned inner = 0; inner < PACKED_INNER; ++inner)
      packed_nibble_set(
          packed_batch_vectors + row * PACKED_VECTOR_STRIDE,
          PACKED_VECTOR_OFFSET + inner,
          (uint8_t)((7 * row + 3 * inner + tag) & 0xf));
  for (unsigned inner = 0; inner < PACKED_INNER; ++inner)
    for (unsigned col = 0; col < PACKED_OUTPUT_BYTES * 2; ++col)
      packed_nibble_set(
          packed_batch_matrix + inner * PACKED_OUTPUT_BYTES, col,
          (uint8_t)((5 * inner + 11 * col + tag) & 0xf));
  for (unsigned i = 0; i < sizeof(packed_batch_output); ++i)
    packed_batch_output[i] = packed_batch_reference[i] =
        (uint8_t)(13 * i + tag);
  for (unsigned row = 0; row < PACKED_BATCH; ++row)
    for (unsigned col = 0; col < PACKED_OUTPUT_BYTES * 2; ++col)
      for (unsigned inner = 0; inner < PACKED_INNER; ++inner) {
        uint8_t old_value = packed_nibble_get(
            packed_batch_reference + row * PACKED_OUTPUT_BYTES, col);
        uint8_t product = gf16_mul(
            packed_nibble_get(
                packed_batch_vectors + row * PACKED_VECTOR_STRIDE,
                PACKED_VECTOR_OFFSET + inner),
            packed_nibble_get(
                packed_batch_matrix + inner * PACKED_OUTPUT_BYTES, col));
        packed_nibble_set(
            packed_batch_reference + row * PACKED_OUTPUT_BYTES, col,
            old_value ^ product);
      }
  if (!xmpk_gf16_batch_matvec_add(
          packed_batch_vectors, PACKED_VECTOR_STRIDE,
          PACKED_VECTOR_OFFSET, packed_batch_matrix,
          packed_batch_output, PACKED_BATCH, PACKED_INNER,
          PACKED_OUTPUT_BYTES))
    tohost_exit(2400 + tag);
  if (memcmp(packed_batch_output, packed_batch_reference,
             sizeof(packed_batch_reference)) != 0)
    tohost_exit(2500 + tag);
}

static void run_packed_gemm_case(unsigned tag) {
  memset(packed_gemm_scalars, 0, sizeof(packed_gemm_scalars));
  memset(packed_gemm_vectors, 0, sizeof(packed_gemm_vectors));
  for (unsigned row = 0; row < GEMM_ROWS; ++row)
    for (unsigned inner = 0; inner < GEMM_INNER; ++inner)
      packed_gemm_scalars[row + inner * 4] =
          (uint8_t)((5 * row + 7 * inner + tag) & 0xf);
  for (unsigned inner = 0; inner < GEMM_INNER; ++inner)
    for (unsigned word = 0; word < GEMM_WORDS; ++word)
      for (unsigned lane = 0; lane < 16; ++lane)
        packed_gemm_vectors[inner * GEMM_VECTOR_STRIDE + word] |=
            (uint64_t)((3 * inner + 11 * word + lane + tag) & 0xf)
            << (4 * lane);
  for (unsigned i = 0; i < GEMM_ROWS * GEMM_ACC_STRIDE; ++i)
    packed_gemm_output[i] = packed_gemm_reference[i] =
        UINT64_C(0xfedcba9876543210) ^ ((uint64_t)(i + tag) << 7);
  for (unsigned row = 0; row < GEMM_ROWS; ++row)
    for (unsigned inner = 0; inner < GEMM_INNER; ++inner)
      for (unsigned word = 0; word < GEMM_WORDS; ++word)
        for (unsigned lane = 0; lane < 16; ++lane) {
          uint8_t value = (uint8_t)(
              packed_gemm_vectors[inner * GEMM_VECTOR_STRIDE + word] >>
              (4 * lane)) & 0xf;
          packed_gemm_reference[row * GEMM_ACC_STRIDE + word] ^=
              (uint64_t)gf16_mul(
                  packed_gemm_scalars[row + inner * 4], value)
              << (4 * lane);
        }
  if (!xmpk_gf16_packed_gemm_add(
          packed_gemm_scalars, 1, 4, packed_gemm_vectors,
          GEMM_VECTOR_STRIDE, packed_gemm_output, GEMM_ACC_STRIDE,
          GEMM_ROWS, GEMM_INNER, GEMM_WORDS))
    tohost_exit(2600 + tag);
  if (memcmp(packed_gemm_output, packed_gemm_reference,
             sizeof(packed_gemm_reference)) != 0)
    tohost_exit(2700 + tag);
}

static void run_upper_transpose_case(unsigned tag) {
  memset(upper_left_columns, 0, sizeof(upper_left_columns));
  memset(upper_right, 0, sizeof(upper_right));
  memset(upper_output, 0, sizeof(upper_output));
  memset(upper_reference, 0, sizeof(upper_reference));
  memset(upper_full, 0, sizeof(upper_full));
  for (unsigned row = 0; row < UPPER_DIM; ++row)
    for (unsigned inner = 0; inner < UPPER_INNER; ++inner)
      packed_nibble_set(
          upper_left_columns + row * UPPER_LEFT_STRIDE, inner,
          (uint8_t)((7 * row + 5 * inner + tag) & 0xf));
  for (unsigned inner = 0; inner < UPPER_INNER; ++inner)
    for (unsigned col = 0; col < UPPER_DIM; ++col)
      for (unsigned coefficient = 0;
           coefficient < UPPER_COEFF_BYTES * 2; ++coefficient)
        packed_nibble_set(
            upper_right +
                (inner * UPPER_DIM + col) * UPPER_COEFF_BYTES,
            coefficient,
            (uint8_t)((3 * inner + 11 * col + coefficient + tag) &
                      0xf));
  for (unsigned row = 0; row < UPPER_DIM; ++row)
    for (unsigned col = 0; col < UPPER_DIM; ++col)
      for (unsigned coefficient = 0;
           coefficient < UPPER_COEFF_BYTES * 2; ++coefficient)
        for (unsigned inner = 0; inner < UPPER_INNER; ++inner) {
          uint8_t *destination =
              upper_full +
              (row * UPPER_DIM + col) * UPPER_COEFF_BYTES;
          uint8_t old_value =
              packed_nibble_get(destination, coefficient);
          uint8_t product = gf16_mul(
              packed_nibble_get(
                  upper_left_columns + row * UPPER_LEFT_STRIDE,
                  inner),
              packed_nibble_get(
                  upper_right +
                      (inner * UPPER_DIM + col) * UPPER_COEFF_BYTES,
                  coefficient));
          packed_nibble_set(destination, coefficient,
                            old_value ^ product);
        }
  unsigned block = 0;
  for (unsigned row = 0; row < UPPER_DIM; ++row)
    for (unsigned col = row; col < UPPER_DIM; ++col) {
      for (unsigned byte = 0; byte < UPPER_COEFF_BYTES; ++byte) {
        uint8_t value =
            upper_full[(row * UPPER_DIM + col) *
                           UPPER_COEFF_BYTES +
                       byte];
        if (row != col)
          value ^= upper_full[(col * UPPER_DIM + row) *
                                  UPPER_COEFF_BYTES +
                              byte];
        upper_reference[block * UPPER_COEFF_BYTES + byte] = value;
      }
      ++block;
    }
  if (!xmpk_gf16_upper_transpose_matmul(
          upper_left_columns, UPPER_LEFT_STRIDE, upper_right,
          upper_output, UPPER_INNER, UPPER_DIM, UPPER_COEFF_BYTES))
    tohost_exit(2800 + tag);
  if (memcmp(upper_output, upper_reference,
             sizeof(upper_reference)) != 0)
    tohost_exit(2900 + tag);
}

static void run_gf256_matvec_case(unsigned tag, unsigned matrix_type) {
  for (unsigned i = 0; i < 16 * 16; ++i)
    gf256_matrix[i] = (uint8_t)(13 * i + 7 * tag + 1);
  for (unsigned i = 0; i < 16; ++i)
    gf256_vector[i] = (uint8_t)(17 * i + 5 * tag + 3);
  memset(gf256_output, 0, sizeof(gf256_output));
  memset(gf256_reference, 0, sizeof(gf256_reference));
  for (unsigned row = 0; row < 16; ++row) {
    unsigned lo = matrix_type == 1 ? row : 0;
    unsigned hi = matrix_type == 2 ? row + 1 : 16;
    for (unsigned col = lo; col < hi; ++col)
      gf256_reference[row] ^=
          gf256_mul(gf256_matrix[row * 16 + col], gf256_vector[col]);
  }
  if (!xmpk_gf256_matvec(gf256_matrix, gf256_vector, gf256_output,
                         16, matrix_type))
    tohost_exit(1000 + tag);
  if (memcmp(gf256_output, gf256_reference, 16) != 0)
    tohost_exit(1100 + tag);
}

static void run_gf256_matvec_batch_case(unsigned tag,
                                        unsigned matrix_type) {
  for (unsigned item = 0; item < GF256_BATCH; ++item)
    for (unsigned i = 0; i < 16 * 16; ++i)
      gf256_batch_matrix[item * 16 * 16 + i] =
          (uint8_t)(13 * i + 19 * item + 7 * tag + 1);
  for (unsigned i = 0; i < 16; ++i)
    gf256_vector[i] = (uint8_t)(17 * i + 5 * tag + 3);
  memset(gf256_batch_output, 0, sizeof(gf256_batch_output));
  memset(gf256_batch_reference, 0, sizeof(gf256_batch_reference));
  for (unsigned item = 0; item < GF256_BATCH; ++item)
    for (unsigned row = 0; row < 16; ++row) {
      unsigned lo = matrix_type == 1 ? row : 0;
      unsigned hi = matrix_type == 2 ? row + 1 : 16;
      for (unsigned col = lo; col < hi; ++col)
        gf256_batch_reference[item * 16 + row] ^=
            gf256_mul(
                gf256_batch_matrix[item * 16 * 16 + row * 16 + col],
                gf256_vector[col]);
    }
  if (!xmpk_gf256_matvec_batch(
          gf256_batch_matrix, 16 * 16, gf256_vector,
          gf256_batch_output, 16, 16, matrix_type, GF256_BATCH))
    tohost_exit(1600 + tag);
  if (memcmp(gf256_batch_output, gf256_batch_reference,
             sizeof(gf256_batch_reference)) != 0)
    tohost_exit(1700 + tag);
}

static void run_gf256_matvec_l5_batch_case(unsigned tag) {
  for (unsigned item = 0; item < GF256_BATCH; ++item)
    for (unsigned i = 0; i < GF256_L5_N * GF256_L5_N; ++i)
      gf256_l5_matrix[
          item * GF256_L5_N * GF256_L5_N + i] =
          (uint8_t)(13 * i + 19 * item + 7 * tag + 1);
  for (unsigned i = 0; i < GF256_L5_N; ++i)
    gf256_l5_vector[i] = (uint8_t)(17 * i + 5 * tag + 3);
  memset(gf256_l5_output, 0, sizeof(gf256_l5_output));
  memset(gf256_l5_reference, 0, sizeof(gf256_l5_reference));
  for (unsigned item = 0; item < GF256_BATCH; ++item)
    for (unsigned row = 0; row < GF256_L5_N; ++row)
      for (unsigned col = 0; col <= row; ++col)
        gf256_l5_reference[item * GF256_L5_N + row] ^=
            gf256_mul(
                gf256_l5_matrix[
                    item * GF256_L5_N * GF256_L5_N +
                    row * GF256_L5_N + col],
                gf256_l5_vector[col]);
  if (!xmpk_gf256_matvec_batch(
          gf256_l5_matrix, GF256_L5_N * GF256_L5_N,
          gf256_l5_vector, gf256_l5_output, GF256_L5_N,
          GF256_L5_N, 2, GF256_BATCH))
    tohost_exit(3300 + tag);
  if (memcmp(gf256_l5_output, gf256_l5_reference,
             sizeof(gf256_l5_reference)) != 0)
    tohost_exit(3400 + tag);
}

static void run_gf256_matprod_case(unsigned tag) {
  for (unsigned i = 0; i < GF256_MATPROD_WIDTH; ++i)
    gf256_matprod_scalars[i] =
        (uint8_t)(17 * i + 5 * tag + 3);
  for (unsigned i = 0; i < sizeof(gf256_matprod_vectors); ++i)
    gf256_matprod_vectors[i] =
        (uint8_t)(13 * i + 7 * tag + 1);
  memset(gf256_matprod_output, 0, sizeof(gf256_matprod_output));
  memset(gf256_matprod_reference, 0,
         sizeof(gf256_matprod_reference));
  for (unsigned inner = 0; inner < GF256_MATPROD_WIDTH; ++inner)
    for (unsigned byte = 0; byte < GF256_MATPROD_BYTES; ++byte)
      gf256_matprod_reference[byte] ^=
          gf256_mul(
              gf256_matprod_vectors[
                  inner * GF256_MATPROD_BYTES + byte],
              gf256_matprod_scalars[inner]);
  if (!xmpk_gf256_matprod(
          gf256_matprod_vectors, GF256_MATPROD_BYTES,
          GF256_MATPROD_WIDTH, gf256_matprod_scalars,
          gf256_matprod_output))
    tohost_exit(3500 + tag);
  if (memcmp(gf256_matprod_output, gf256_matprod_reference,
             sizeof(gf256_matprod_reference)) != 0)
    tohost_exit(3600 + tag);
}

static void run_gf256_matprod_odd_case(unsigned tag) {
  for (unsigned i = 0; i < GF256_MATPROD_ODD_WIDTH; ++i)
    gf256_matprod_odd_scalars[i] =
        (uint8_t)(17 * i + 5 * tag + 3);
  for (unsigned i = 0; i < sizeof(gf256_matprod_odd_vectors); ++i)
    gf256_matprod_odd_vectors[i] =
        (uint8_t)(13 * i + 7 * tag + 1);
  memset(gf256_matprod_odd_output, 0,
         sizeof(gf256_matprod_odd_output));
  memset(gf256_matprod_odd_reference, 0,
         sizeof(gf256_matprod_odd_reference));
  for (unsigned inner = 0; inner < GF256_MATPROD_ODD_WIDTH; ++inner)
    for (unsigned byte = 0; byte < GF256_MATPROD_ODD_BYTES; ++byte)
      gf256_matprod_odd_reference[byte] ^=
          gf256_mul(
              gf256_matprod_odd_vectors[
                  inner * GF256_MATPROD_ODD_BYTES + byte],
              gf256_matprod_odd_scalars[inner]);
  if (!xmpk_gf256_matprod(
          gf256_matprod_odd_vectors, GF256_MATPROD_ODD_BYTES,
          GF256_MATPROD_ODD_WIDTH, gf256_matprod_odd_scalars,
          gf256_matprod_odd_output))
    tohost_exit(3700 + tag);
  if (memcmp(gf256_matprod_odd_output,
             gf256_matprod_odd_reference,
             sizeof(gf256_matprod_odd_reference)) != 0)
    tohost_exit(3800 + tag);
}

static void run_case(unsigned tag, unsigned rows, unsigned inner,
                     unsigned cols, int add) {
  for (unsigned i = 0; i < rows * inner; ++i)
    left[i] = (uint8_t)((7 * i + 3 * tag + 1) & 0xf);
  for (unsigned i = 0; i < inner * cols; ++i)
    right[i] = (uint8_t)((11 * i + 5 * tag + 2) & 0xf);
  for (unsigned i = 0; i < rows * cols; ++i)
    output[i] = reference[i] = add ? (uint8_t)((i + tag) & 0xf) : 0;
  for (unsigned row = 0; row < rows; ++row)
    for (unsigned col = 0; col < cols; ++col)
      for (unsigned k = 0; k < inner; ++k)
        reference[row * cols + col] ^=
            gf16_mul(left[row * inner + k], right[k * cols + col]);
  if (!xmpk_gf16_matmul_bytes(left, right, output, rows, inner, cols, add))
    tohost_exit(100 + tag);
  if (memcmp(output, reference, rows * cols) != 0)
    tohost_exit(200 + tag);
}

static void run_linxfm_case(unsigned tag, unsigned dimension,
                            unsigned cols, int add) {
  run_case(tag, dimension, dimension, cols, add);
  for (unsigned i = 0; i < dimension * cols; ++i)
    output[i] = add ? (uint8_t)((i + tag) & 0xf) : 0;
  if (!xmpk_gf16_linxfm_matmul_add(left, right, output, dimension,
                                    cols, add))
    tohost_exit(300 + tag);
  if (memcmp(output, reference, dimension * cols) != 0)
    tohost_exit(400 + tag);
}

static void run_linxfm_batch_case(unsigned tag, int add) {
  for (unsigned i = 0; i < BATCH_DIM * BATCH_DIM; ++i)
    left[i] = (uint8_t)((5 * i + tag + 1) & 0xf);
  for (unsigned item = 0; item < BATCH_COUNT; ++item) {
    uint8_t *right_item = batch_right + item * BATCH_RIGHT_STRIDE;
    uint8_t *output_item = batch_output + item * BATCH_RESULT_STRIDE;
    uint8_t *reference_item = batch_reference + item * BATCH_RESULT_STRIDE;
    for (unsigned i = 0; i < BATCH_DIM * BATCH_DIM; ++i)
      right_item[i] = (uint8_t)((7 * i + 3 * item + tag) & 0xf);
    for (unsigned i = 0; i < BATCH_RESULT_STRIDE; ++i)
      output_item[i] = reference_item[i] =
          add ? (uint8_t)((i + item + tag) & 0xf) : 0;
    for (unsigned row = 0; row < BATCH_DIM; ++row)
      for (unsigned col = 0; col < BATCH_DIM; ++col)
        for (unsigned k = 0; k < BATCH_DIM; ++k)
          reference_item[row * BATCH_DIM + col] ^=
              gf16_mul(left[row * BATCH_DIM + k],
                       right_item[k * BATCH_DIM + col]);
  }
  if (!xmpk_gf16_linxfm_batch_same_left(
          left, batch_right, BATCH_RIGHT_STRIDE,
          batch_output, BATCH_RESULT_STRIDE,
          BATCH_DIM, BATCH_DIM, BATCH_COUNT, add))
    tohost_exit(500 + tag);
  for (unsigned item = 0; item < BATCH_COUNT; ++item)
    if (memcmp(batch_output + item * BATCH_RESULT_STRIDE,
               batch_reference + item * BATCH_RESULT_STRIDE,
               BATCH_DIM * BATCH_DIM) != 0)
      tohost_exit(600 + tag);
}

static void run_linxfm_prepared_reuse_case(unsigned tag) {
  for (unsigned item = 0; item < BATCH_COUNT; ++item) {
    uint8_t *right_item = batch_right + item * BATCH_RIGHT_STRIDE;
    for (unsigned i = 0; i < BATCH_DIM * BATCH_DIM; ++i)
      right_item[i] = (uint8_t)((9 * i + item + tag) & 0xf);
  }
  if (!xmpk_gf16_linxfm_prepare_right_batch(
          batch_right, BATCH_RIGHT_STRIDE,
          BATCH_DIM, BATCH_DIM, BATCH_COUNT))
    tohost_exit(700 + tag);
  for (unsigned pass = 0; pass < 2; ++pass) {
    for (unsigned i = 0; i < BATCH_DIM * BATCH_DIM; ++i)
      left[i] = (uint8_t)((3 * i + 5 * pass + tag) & 0xf);
    memset(batch_output, 0, sizeof(batch_output));
    memset(batch_reference, 0, sizeof(batch_reference));
    for (unsigned item = 0; item < BATCH_COUNT; ++item) {
      uint8_t *right_item = batch_right + item * BATCH_RIGHT_STRIDE;
      uint8_t *reference_item =
          batch_reference + item * BATCH_RESULT_STRIDE;
      for (unsigned row = 0; row < BATCH_DIM; ++row)
        for (unsigned col = 0; col < BATCH_DIM; ++col)
          for (unsigned k = 0; k < BATCH_DIM; ++k)
            reference_item[row * BATCH_DIM + col] ^=
                gf16_mul(left[row * BATCH_DIM + k],
                         right_item[k * BATCH_DIM + col]);
    }
    if (!xmpk_gf16_linxfm_apply_prepared(
            left, batch_output, BATCH_RESULT_STRIDE, 0))
      tohost_exit(800 + tag);
    for (unsigned item = 0; item < BATCH_COUNT; ++item)
      if (memcmp(batch_output + item * BATCH_RESULT_STRIDE,
                 batch_reference + item * BATCH_RESULT_STRIDE,
                 BATCH_DIM * BATCH_DIM) != 0)
        tohost_exit(900 + tag);
  }
}

int main(void) {
#ifdef XMPK_EXTGEMM_QUICK
  run_extgemm_case(12, 3, 0);
  run_extgemm_case(13, 3, 1);
  run_extgemm_case(14, 10, 1);
  return 0;
#endif
  gf256_mul_table_init();
  run_case(1, 80, 81, 1, 0);
  run_case(22, 142, 142, 1, 0);
  run_linxfm_case(2, 4, 4, 0);
  run_linxfm_case(3, 4, 4, 1);
  run_linxfm_case(4, 4, 6, 1);
  run_linxfm_case(5, 6, 4, 1);
  run_linxfm_batch_case(6, 0);
  run_linxfm_batch_case(7, 1);
  run_linxfm_prepared_reuse_case(8);
  run_gf256_matvec_case(9, 0);
  run_gf256_packed_case(27);
  run_gf256_matvec_case(10, 1);
  run_gf256_matvec_case(11, 2);
  run_gf256_matvec_batch_case(16, 0);
  run_gf256_matvec_batch_case(17, 2);
  run_gf256_matvec_l5_batch_case(23);
  run_gf256_matprod_case(24);
  run_gf256_matprod_odd_case(26);
  run_extgemm_case(12, 3, 0);
  run_extgemm_case(13, 3, 1);
  run_extgemm_case(14, 10, 1);
  run_extgemm_l5_case(25);
  run_solve_case(15);
  run_mvec_dot_case(18);
  run_packed_batch_matvec_case(19);
  run_packed_gemm_case(20);
  run_upper_transpose_case(21);
  return 0;
}
