#include "xmpk_asm.h"

#include <stdint.h>
#include <string.h>

#define XMPK_SOLVE_MAX_N 64
#define XMPK_SOLVE_MAX_WORDS 5

static uint64_t solve_matrix[XMPK_SOLVE_MAX_N * XMPK_SOLVE_MAX_WORDS]
    __attribute__((aligned(4096)));
static uint64_t solve_result[(XMPK_SOLVE_MAX_N + 15) / 16]
    __attribute__((aligned(4096)));
#define XMPK_GF256_MAX_N 128
#define XMPK_GF256_MAX_BATCH 3
static uint64_t matvec_matrix[XMPK_GF256_MAX_BATCH * XMPK_GF256_MAX_N *
                              (XMPK_GF256_MAX_N / 8)]
    __attribute__((aligned(4096)));
static uint64_t matvec_vector[XMPK_GF256_MAX_N]
    __attribute__((aligned(4096)));
static uint64_t matvec_result[XMPK_GF256_MAX_BATCH * XMPK_GF256_MAX_N]
    __attribute__((aligned(4096)));
#define XMPK_GF256_MATPROD_MAX_BYTES (96 * 96)
#define XMPK_GF256_MATPROD_MAX_WORDS \
  ((XMPK_GF256_MATPROD_MAX_BYTES + 7) / 8)
static uint64_t matprod_scalars[(255 + 7) / 8]
    __attribute__((aligned(4096)));
static uint64_t matprod_vectors[255 * XMPK_GF256_MATPROD_MAX_WORDS]
    __attribute__((aligned(4096)));
static uint64_t matprod_result[XMPK_GF256_MATPROD_MAX_WORDS]
    __attribute__((aligned(4096)));
#define XMPK_GF256_MATPROD_BATCH_MAX_ROWS 96
#define XMPK_GF256_MATPROD_BATCH_MAX_BYTES 96
static uint64_t matprod_batch_scalars[
    XMPK_GF256_MATPROD_BATCH_MAX_ROWS * ((255 + 7) / 8)]
    __attribute__((aligned(4096)));
static uint64_t matprod_batch_result[
    XMPK_GF256_MATPROD_BATCH_MAX_ROWS *
    (XMPK_GF256_MATPROD_BATCH_MAX_BYTES / 8)]
    __attribute__((aligned(4096)));
#define XMPK_GF16_MAX_ROWS 160
#define XMPK_GF16_MAX_INNER 160
#define XMPK_GF16_MAX_COLS 16
static uint64_t gf16_left[XMPK_GF16_MAX_ROWS *
                          ((XMPK_GF16_MAX_INNER + 15) / 16)]
    __attribute__((aligned(4096)));
static uint64_t gf16_right[XMPK_GF16_MAX_INNER *
                           ((XMPK_GF16_MAX_COLS + 15) / 16)]
    __attribute__((aligned(4096)));
static uint64_t gf16_result[XMPK_GF16_MAX_ROWS *
                            ((XMPK_GF16_MAX_COLS + 15) / 16)]
    __attribute__((aligned(4096)));
#define XMPK_MVEC_MAX_COUNT 160
#define XMPK_MVEC_MAX_WORDS 16
static uint64_t mvec_scalars[(XMPK_MVEC_MAX_COUNT + 15) / 16]
    __attribute__((aligned(4096)));
static uint64_t mvec_vectors[XMPK_MVEC_MAX_COUNT * XMPK_MVEC_MAX_WORDS]
    __attribute__((aligned(4096)));
static uint64_t mvec_result[XMPK_MVEC_MAX_WORDS]
    __attribute__((aligned(4096)));
#define XMPK_BATCH_VEC_MAX_ROWS 64
#define XMPK_BATCH_VEC_MAX_WORDS 16
static uint64_t batch_vec_left[XMPK_BATCH_VEC_MAX_ROWS *
                               ((XMPK_GF16_MAX_INNER + 15) / 16)]
    __attribute__((aligned(4096)));
static uint64_t batch_vec_right[XMPK_GF16_MAX_INNER *
                                XMPK_BATCH_VEC_MAX_WORDS]
    __attribute__((aligned(4096)));
static uint64_t batch_vec_result[XMPK_BATCH_VEC_MAX_ROWS *
                                 XMPK_BATCH_VEC_MAX_WORDS]
    __attribute__((aligned(4096)));
#define XMPK_PACKED_GEMM_MAX_ROWS 64
#define XMPK_PACKED_GEMM_MAX_WORDS 128
static uint64_t packed_gemm_left[XMPK_PACKED_GEMM_MAX_ROWS *
                                 ((XMPK_GF16_MAX_INNER + 15) / 16)]
    __attribute__((aligned(4096)));
static uint64_t packed_gemm_right[XMPK_GF16_MAX_INNER *
                                  XMPK_PACKED_GEMM_MAX_WORDS]
    __attribute__((aligned(4096)));
static uint64_t packed_gemm_result[XMPK_PACKED_GEMM_MAX_ROWS *
                                   XMPK_PACKED_GEMM_MAX_WORDS]
    __attribute__((aligned(4096)));
#define XMPK_UPPER_MAX_ROWS 64
#define XMPK_UPPER_MAX_WORDS 256
static uint64_t upper_left[XMPK_UPPER_MAX_ROWS *
                           ((XMPK_GF16_MAX_INNER + 15) / 16)]
    __attribute__((aligned(4096)));
static uint64_t upper_right[XMPK_GF16_MAX_INNER * XMPK_UPPER_MAX_WORDS]
    __attribute__((aligned(4096)));
static uint64_t upper_result[XMPK_UPPER_MAX_ROWS * XMPK_UPPER_MAX_WORDS]
    __attribute__((aligned(4096)));
static uint64_t linxfm_transform[16] __attribute__((aligned(4096)));
static uint64_t linxfm_input[255] __attribute__((aligned(4096)));
static uint64_t linxfm_output[255] __attribute__((aligned(4096)));
static unsigned linxfm_prepared_dimension;
static unsigned linxfm_prepared_cols;
static unsigned linxfm_prepared_batch;
#define XMPK_EXT_MAX_DIM 128
static xmpk_gf127_slot ext_a[XMPK_EXT_MAX_DIM * XMPK_EXT_MAX_DIM]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot ext_b[XMPK_EXT_MAX_DIM * XMPK_EXT_MAX_DIM]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot ext_c[XMPK_EXT_MAX_DIM * XMPK_EXT_MAX_DIM]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot ext_prepared_a[XMPK_EXT_MAX_DIM * XMPK_EXT_MAX_DIM]
    __attribute__((aligned(4096)));
static unsigned ext_prepared_m;
static unsigned ext_prepared_k;
static unsigned ext_prepared_degree;
static size_t ext_used_a, ext_used_b, ext_used_c, ext_used_prepared;

static void ext_record_used(size_t *used, size_t count) {
#if defined(QRUOV_KEYGEN_SHORT_TEST)
  if (*used < count)
    *used = count;
#else
  (void)used;
  (void)count;
#endif
}

static inline void ext_pack(xmpk_gf127_slot *dst, const uint8_t *src,
                            unsigned degree) {
  uint64_t lo = (uint64_t)src[0] |
                (uint64_t)src[1] << 8 |
                (uint64_t)src[2] << 16;
  if (degree == 10) {
    for (unsigned i = 3; i < 8; ++i)
      lo |= (uint64_t)src[i] << (8 * i);
    dst->hi = (uint64_t)src[8] | (uint64_t)src[9] << 8;
  }
  dst->lo = lo;
}

static inline void ext_unpack(uint8_t *dst, const xmpk_gf127_slot *src,
                              unsigned degree) {
  uint64_t lo = src->lo;
  dst[0] = (uint8_t)lo;
  dst[1] = (uint8_t)(lo >> 8);
  dst[2] = (uint8_t)(lo >> 16);
  if (degree == 10) {
    for (unsigned i = 3; i < 8; ++i)
      dst[i] = (uint8_t)(lo >> (8 * i));
    uint64_t hi = src->hi;
    dst[8] = (uint8_t)hi;
    dst[9] = (uint8_t)(hi >> 8);
  }
}

static uint8_t packed_get(const uint8_t *data, unsigned index) {
  uint8_t byte = data[index >> 1];
  return (index & 1) ? byte >> 4 : byte & 0xf;
}

static void word_set(uint64_t *data, unsigned index, uint8_t value) {
  unsigned shift = 4 * (index & 15);
  uint64_t *word = &data[index >> 4];
  *word = (*word & ~(UINT64_C(0xf) << shift)) |
          ((uint64_t)(value & 0xf) << shift);
}

int xmpk_gf16_solve_colmajor(uint8_t *matrix, uint8_t *constant,
                             unsigned n) {
  unsigned row_words;
  uint64_t status;

  if (n == 0 || n > XMPK_SOLVE_MAX_N)
    return 0;
  row_words = (n + 16) / 16;
  memset(solve_matrix, 0, sizeof(solve_matrix));
  memset(solve_result, 0, sizeof(solve_result));
  if ((n & 15u) == 0) {
    unsigned column_stride = n / 2;
    for (unsigned row_base = 0; row_base < n; row_base += 16)
      for (unsigned col_base = 0; col_base < n; col_base += 16) {
        uint64_t rows[16] = {0};
        for (unsigned col = 0; col < 16; ++col) {
          uint64_t packed_rows;
          memcpy(&packed_rows,
                 matrix + (col_base + col) * column_stride + row_base / 2,
                 sizeof(packed_rows));
          for (unsigned row = 0; row < 16; ++row)
            rows[row] |= ((packed_rows >> (4 * row)) & UINT64_C(0xf))
                         << (4 * col);
        }
        for (unsigned row = 0; row < 16; ++row)
          solve_matrix[(row_base + row) * row_words + col_base / 16] =
              rows[row];
      }
    for (unsigned row = 0; row < n; ++row)
      word_set(solve_matrix + row * row_words, n,
               packed_get(constant, row));
  } else {
    for (unsigned row = 0; row < n; ++row) {
      uint64_t *dst = solve_matrix + row * row_words;
      for (unsigned col = 0; col < n; ++col)
        word_set(dst, col,
                 packed_get(matrix + col * ((n + 1) / 2), row));
      word_set(dst, n, packed_get(constant, row));
    }
  }
  xmpk_solveq_setup(solve_matrix, solve_result);
  status = xmpk_solveq_run(n);
  if (status >> 63)
    return 0;
  memcpy(constant, solve_result, (n + 1) / 2);
  return 1;
}

int xmpk_gf256_matvec(const uint8_t *matrix, const uint8_t *vector,
                      uint8_t *result, unsigned n, unsigned matrix_type) {
  return xmpk_gf256_matvec_batch(matrix, n * n, vector, result, n, n,
                                 matrix_type, 1);
}

int xmpk_gf256_matvec_batch(const uint8_t *matrices,
                            size_t matrix_stride, const uint8_t *vector,
                            uint8_t *results, size_t result_stride,
                            unsigned n, unsigned matrix_type,
                            unsigned batch) {
  unsigned row_words;
  unsigned rows;
  const uint64_t *matrix_words;

  if (n == 0 || n > XMPK_GF256_MAX_N || (n & 7u) != 0 ||
      matrix_type > 2 ||
      batch == 0 || batch > XMPK_GF256_MAX_BATCH ||
      matrix_stride < n * n || result_stride < n)
    return 0;
  row_words = n / 8;
  rows = batch * n;
  if (matrix_type == 0 && matrix_stride == n * n &&
      ((uintptr_t)matrices & 7u) == 0) {
    matrix_words = (const uint64_t *)(const void *)matrices;
  } else {
    memset(matvec_matrix, 0, rows * row_words * sizeof(uint64_t));
    for (unsigned item = 0; item < batch; ++item)
      for (unsigned row = 0; row < n; ++row) {
        unsigned lo = matrix_type == 1 ? row : 0;
        unsigned hi = matrix_type == 2 ? row + 1 : n;
        uint8_t *dst = (uint8_t *)(void *)(
            matvec_matrix + (item * n + row) * row_words);
        const uint8_t *source =
            matrices + item * matrix_stride + row * n;
        for (unsigned col = lo; col < hi; ++col)
          dst[col] = source[col];
      }
    matrix_words = matvec_matrix;
  }
  memset(matvec_vector, 0, n * sizeof(uint64_t));
  memset(matvec_result, 0, rows * sizeof(uint64_t));
  for (unsigned row = 0; row < n; ++row)
    matvec_vector[row] = vector[row];
  for (unsigned row_offset = 0; row_offset < rows; ) {
    unsigned chunk_rows = rows - row_offset;
    uint64_t count;
    if (chunk_rows > 255)
      chunk_rows = 255;
    xmpk_qgemm_setup(matrix_words + row_offset * row_words,
                     matvec_vector, matvec_result + row_offset);
    count = xmpk_qgemm_run(chunk_rows, n, 1, 1);
    if (count != (uint64_t)chunk_rows * n * 8)
      return 0;
    row_offset += chunk_rows;
  }
  for (unsigned item = 0; item < batch; ++item)
    for (unsigned row = 0; row < n; ++row)
      results[item * result_stride + row] =
          (uint8_t)matvec_result[item * n + row];
  return 1;
}

int xmpk_gf256_matprod(const uint8_t *vectors, size_t vector_bytes,
                       unsigned count, const uint8_t *scalars,
                       uint8_t *result) {
  const uint64_t *matrix_words;
  uint64_t *result_words;
  unsigned words;
  uint64_t operations;

  if (vector_bytes == 0 ||
      vector_bytes > XMPK_GF256_MATPROD_MAX_BYTES ||
      count == 0 || count > 255)
    return 0;
  words = (unsigned)((vector_bytes + 7) / 8);
  memset(matprod_scalars, 0, sizeof(matprod_scalars));
  for (unsigned i = 0; i < count; ++i)
    ((uint8_t *)(void *)matprod_scalars)[i] = scalars[i];
  if ((vector_bytes & 7u) == 0 &&
      (((uintptr_t)vectors | (uintptr_t)result) & 7u) == 0) {
    matrix_words = (const uint64_t *)(const void *)vectors;
    result_words = (uint64_t *)(void *)result;
    memset(result, 0, vector_bytes);
  } else {
    memset(matprod_vectors, 0,
           count * words * sizeof(uint64_t));
    memset(matprod_result, 0, words * sizeof(uint64_t));
    for (unsigned i = 0; i < count; ++i)
      memcpy((uint8_t *)(void *)(matprod_vectors + i * words),
             vectors + i * vector_bytes, vector_bytes);
    matrix_words = matprod_vectors;
    result_words = matprod_result;
  }
  xmpk_qgemm_setup(matprod_scalars,
                   matrix_words, result_words);
  operations = xmpk_qgemm_run(1, count, words, 1);
  if (operations != (uint64_t)count * words * 8)
    return 0;
  if (result_words == matprod_result)
    memcpy(result, matprod_result, vector_bytes);
  return 1;
}

int xmpk_gf256_matprod_batch(
    const uint8_t *vectors, size_t vector_bytes, unsigned count,
    const uint8_t *scalars, size_t scalar_stride, uint8_t *results,
    size_t result_stride, unsigned rows) {
  const uint64_t *matrix_words;
  uint64_t *result_words;
  unsigned scalar_words;
  unsigned words;
  uint64_t operations;

  if (vector_bytes == 0 ||
      vector_bytes > XMPK_GF256_MATPROD_BATCH_MAX_BYTES ||
      count == 0 || count > 255 ||
      rows == 0 || rows > XMPK_GF256_MATPROD_BATCH_MAX_ROWS ||
      scalar_stride < count || result_stride < vector_bytes)
    return 0;
  words = (unsigned)((vector_bytes + 7) / 8);
  scalar_words = (count + 7) / 8;
  memset(matprod_batch_scalars, 0,
         rows * scalar_words * sizeof(uint64_t));
  for (unsigned row = 0; row < rows; ++row)
    memcpy((uint8_t *)(void *)(matprod_batch_scalars +
                              row * scalar_words),
           scalars + row * scalar_stride, count);

  if ((vector_bytes & 7u) == 0 &&
      ((uintptr_t)vectors & 7u) == 0) {
    matrix_words = (const uint64_t *)(const void *)vectors;
  } else {
    memset(matprod_vectors, 0,
           count * words * sizeof(uint64_t));
    for (unsigned i = 0; i < count; ++i)
      memcpy((uint8_t *)(void *)(matprod_vectors + i * words),
             vectors + i * vector_bytes, vector_bytes);
    matrix_words = matprod_vectors;
  }

  if ((vector_bytes & 7u) == 0 &&
      result_stride == vector_bytes &&
      ((uintptr_t)results & 7u) == 0) {
    result_words = (uint64_t *)(void *)results;
    memset(results, 0, rows * vector_bytes);
  } else {
    result_words = matprod_batch_result;
    memset(result_words, 0,
           rows * words * sizeof(uint64_t));
  }

  xmpk_qgemm_setup(matprod_batch_scalars, matrix_words, result_words);
  operations = xmpk_qgemm_run(rows, count, words, 1);
  if (operations != (uint64_t)rows * count * words * 8)
    return 0;
  if (result_words == matprod_batch_result)
    for (unsigned row = 0; row < rows; ++row)
      memcpy(results + row * result_stride,
             (const uint8_t *)(const void *)(result_words + row * words),
             vector_bytes);
  return 1;
}

int xmpk_gf16_matmul_bytes(const uint8_t *left, const uint8_t *right,
                           uint8_t *result, unsigned rows, unsigned inner,
                           unsigned cols, int add) {
  unsigned left_words;
  unsigned result_words;
  uint64_t count;

  if (rows == 0 || rows > XMPK_GF16_MAX_ROWS ||
      inner == 0 || inner > XMPK_GF16_MAX_INNER ||
      cols == 0 || cols > XMPK_GF16_MAX_COLS)
    return 0;
  left_words = (inner + 15) / 16;
  result_words = (cols + 15) / 16;
  memset(gf16_left, 0, rows * left_words * sizeof(uint64_t));
  memset(gf16_right, 0, inner * result_words * sizeof(uint64_t));
  memset(gf16_result, 0, rows * result_words * sizeof(uint64_t));
  for (unsigned row = 0; row < rows; ++row)
    for (unsigned col = 0; col < inner; ++col)
      word_set(gf16_left + row * left_words, col,
               left[row * inner + col]);
  for (unsigned row = 0; row < inner; ++row)
    for (unsigned col = 0; col < cols; ++col)
      word_set(gf16_right + row * result_words, col,
               right[row * cols + col]);

  xmpk_qgemm_setup(gf16_left, gf16_right, gf16_result);
  count = xmpk_qgemm_run(rows, inner, result_words, 0);
  if (count != (uint64_t)rows * inner * result_words * 16)
    return 0;
  for (unsigned row = 0; row < rows; ++row)
    for (unsigned col = 0; col < cols; ++col) {
      uint8_t value = packed_get(
          (const uint8_t *)(const void *)(gf16_result +
                                         row * result_words),
          col);
      if (add)
        result[row * cols + col] ^= value;
      else
        result[row * cols + col] = value;
    }
  return 1;
}

int xmpk_gf16_mvec_dot_add(const uint8_t *scalars,
                           size_t scalar_stride,
                           const uint64_t *vectors,
                           size_t vector_stride_words, uint64_t *acc,
                           unsigned count, unsigned words) {
  unsigned scalar_words;
  uint64_t operations;

  if (count == 0 || count > XMPK_MVEC_MAX_COUNT ||
      words == 0 || words > XMPK_MVEC_MAX_WORDS ||
      scalar_stride == 0 || vector_stride_words < words)
    return 0;
  scalar_words = (count + 15) / 16;
  memset(mvec_scalars, 0, scalar_words * sizeof(uint64_t));
  memset(mvec_vectors, 0, count * words * sizeof(uint64_t));
  memset(mvec_result, 0, words * sizeof(uint64_t));
  for (unsigned i = 0; i < count; ++i) {
    word_set(mvec_scalars, i, scalars[i * scalar_stride]);
    memcpy(mvec_vectors + i * words,
           vectors + i * vector_stride_words,
           words * sizeof(uint64_t));
  }
  xmpk_qgemm_setup(mvec_scalars, mvec_vectors, mvec_result);
  operations = xmpk_qgemm_run(1, count, words, 0);
  if (operations != (uint64_t)count * words * 16)
    return 0;
  for (unsigned i = 0; i < words; ++i)
    acc[i] ^= mvec_result[i];
  return 1;
}

int xmpk_gf16_batch_matvec_add(const uint8_t *vectors,
                               size_t vector_stride,
                               unsigned vector_nibble_offset,
                               const uint8_t *matrix, uint8_t *results,
                               unsigned batch, unsigned inner,
                               unsigned output_bytes) {
  unsigned left_words;
  unsigned output_words;
  uint64_t operations;

  if (batch == 0 || batch > XMPK_BATCH_VEC_MAX_ROWS ||
      inner == 0 || inner > XMPK_GF16_MAX_INNER ||
      output_bytes == 0 || (output_bytes & 7u) != 0 ||
      output_bytes / 8 > XMPK_BATCH_VEC_MAX_WORDS ||
      vector_stride * 2 < vector_nibble_offset + inner)
    return 0;
  left_words = (inner + 15) / 16;
  output_words = output_bytes / 8;
  memset(batch_vec_left, 0,
         batch * left_words * sizeof(uint64_t));
  memset(batch_vec_right, 0,
         inner * output_words * sizeof(uint64_t));
  memset(batch_vec_result, 0,
         batch * output_words * sizeof(uint64_t));
  for (unsigned row = 0; row < batch; ++row)
    for (unsigned col = 0; col < inner; ++col)
      word_set(batch_vec_left + row * left_words, col,
               packed_get(vectors + row * vector_stride,
                          vector_nibble_offset + col));
  memcpy(batch_vec_right, matrix,
         inner * output_words * sizeof(uint64_t));
  xmpk_qgemm_setup(batch_vec_left, batch_vec_right, batch_vec_result);
  operations = xmpk_qgemm_run(batch, inner, output_words, 0);
  if (operations != (uint64_t)batch * inner * output_words * 16)
    return 0;
  for (unsigned row = 0; row < batch; ++row)
    for (unsigned byte = 0; byte < output_bytes; ++byte)
      results[row * output_bytes + byte] ^=
          ((const uint8_t *)(const void *)batch_vec_result)
              [row * output_bytes + byte];
  return 1;
}

int xmpk_gf16_packed_gemm_add(
    const uint8_t *scalars, size_t scalar_row_stride,
    size_t scalar_inner_stride, const uint64_t *vectors,
    size_t vector_row_stride_words, uint64_t *acc,
    size_t acc_row_stride_words, unsigned rows, unsigned inner,
    unsigned words) {
  unsigned left_words;
  uint64_t operations;

  if (rows == 0 || rows > XMPK_PACKED_GEMM_MAX_ROWS ||
      inner == 0 || inner > XMPK_GF16_MAX_INNER ||
      words == 0 || words > XMPK_PACKED_GEMM_MAX_WORDS ||
      scalar_inner_stride == 0 || vector_row_stride_words < words ||
      acc_row_stride_words < words)
    return 0;
  left_words = (inner + 15) / 16;
  memset(packed_gemm_left, 0,
         rows * left_words * sizeof(uint64_t));
  memset(packed_gemm_right, 0,
         inner * words * sizeof(uint64_t));
  memset(packed_gemm_result, 0,
         rows * words * sizeof(uint64_t));
  for (unsigned row = 0; row < rows; ++row)
    for (unsigned col = 0; col < inner; ++col)
      word_set(packed_gemm_left + row * left_words, col,
               scalars[row * scalar_row_stride +
                       col * scalar_inner_stride]);
  for (unsigned row = 0; row < inner; ++row)
    memcpy(packed_gemm_right + row * words,
           vectors + row * vector_row_stride_words,
           words * sizeof(uint64_t));
  xmpk_qgemm_setup(packed_gemm_left, packed_gemm_right,
                   packed_gemm_result);
  operations = xmpk_qgemm_run(rows, inner, words, 0);
  if (operations != (uint64_t)rows * inner * words * 16)
    return 0;
  for (unsigned row = 0; row < rows; ++row)
    for (unsigned word = 0; word < words; ++word)
      acc[row * acc_row_stride_words + word] ^=
          packed_gemm_result[row * words + word];
  return 1;
}

int xmpk_gf16_upper_transpose_matmul(
    const uint8_t *left_columns, size_t left_column_stride,
    const uint8_t *right, uint8_t *upper, unsigned inner,
    unsigned dimension, unsigned coefficient_bytes) {
  unsigned left_words;
  unsigned output_bytes;
  unsigned output_words;
  uint64_t operations;

  if (inner == 0 || inner > XMPK_GF16_MAX_INNER ||
      dimension == 0 || dimension > XMPK_UPPER_MAX_ROWS ||
      coefficient_bytes == 0 || (coefficient_bytes & 7u) != 0 ||
      left_column_stride * 2 < inner)
    return 0;
  output_bytes = dimension * coefficient_bytes;
  output_words = output_bytes / 8;
  if (output_words > XMPK_UPPER_MAX_WORDS)
    return 0;
  left_words = (inner + 15) / 16;
  memset(upper_left, 0,
         dimension * left_words * sizeof(uint64_t));
  memset(upper_right, 0,
         inner * output_words * sizeof(uint64_t));
  memset(upper_result, 0,
         dimension * output_words * sizeof(uint64_t));
  for (unsigned row = 0; row < dimension; ++row)
    for (unsigned col = 0; col < inner; ++col)
      word_set(upper_left + row * left_words, col,
               packed_get(left_columns + row * left_column_stride, col));
  memcpy(upper_right, right,
         inner * output_words * sizeof(uint64_t));
  xmpk_qgemm_setup(upper_left, upper_right, upper_result);
  operations = xmpk_qgemm_run(dimension, inner, output_words, 0);
  if (operations !=
      (uint64_t)dimension * inner * output_words * 16)
    return 0;
  for (unsigned row = 0; row < dimension; ++row) {
    const uint8_t *result_row =
        (const uint8_t *)(const void *)upper_result +
        row * output_bytes;
    uint8_t *destination = upper + row * coefficient_bytes;
    for (unsigned col = 0; col < row; ++col) {
      for (unsigned byte = 0; byte < coefficient_bytes; ++byte)
        destination[byte] ^=
            result_row[col * coefficient_bytes + byte];
      destination += (dimension - col - 1) * coefficient_bytes;
    }
    memcpy(destination, result_row + row * coefficient_bytes,
           (dimension - row) * coefficient_bytes);
  }
  return 1;
}

int xmpk_gf16_linxfm_prepare_right_batch(
    const uint8_t *right, size_t right_stride, unsigned dimension,
    unsigned cols, unsigned batch) {
  unsigned vectors;

  if (dimension == 0 || dimension > 16 || cols == 0 || batch == 0 ||
      batch > 255 / cols)
    return 0;
  vectors = batch * cols;
  memset(linxfm_input, 0, vectors * sizeof(uint64_t));
  for (unsigned item = 0; item < batch; ++item)
    for (unsigned col = 0; col < cols; ++col)
      for (unsigned row = 0; row < dimension; ++row)
        word_set(linxfm_input + item * cols + col, row,
                 right[item * right_stride + row * cols + col]);
  linxfm_prepared_dimension = dimension;
  linxfm_prepared_cols = cols;
  linxfm_prepared_batch = batch;
  return 1;
}

int xmpk_gf16_linxfm_apply_prepared(
    const uint8_t *left, uint8_t *result, size_t result_stride, int add) {
  uint64_t count;
  unsigned dimension = linxfm_prepared_dimension;
  unsigned cols = linxfm_prepared_cols;
  unsigned batch = linxfm_prepared_batch;
  unsigned vectors;

  if (dimension == 0 || cols == 0 || batch == 0)
    return 0;
  vectors = batch * cols;
  memset(linxfm_transform, 0, dimension * sizeof(uint64_t));
  memset(linxfm_output, 0, vectors * sizeof(uint64_t));
  for (unsigned row = 0; row < dimension; ++row)
    for (unsigned col = 0; col < dimension; ++col)
      word_set(linxfm_transform + row, col,
               left[row * dimension + col]);

  xmpk_linxfm_setup(linxfm_transform, linxfm_input, linxfm_output);
  count = xmpk_linxfm_run(dimension, vectors);
  if (count != (uint64_t)dimension * vectors)
    return 0;
  for (unsigned item = 0; item < batch; ++item)
    for (unsigned row = 0; row < dimension; ++row)
      for (unsigned col = 0; col < cols; ++col) {
        uint8_t value = packed_get(
            (const uint8_t *)(const void *)(linxfm_output +
                                           item * cols + col),
            row);
        uint8_t *destination =
            result + item * result_stride + row * cols + col;
        if (add)
          *destination ^= value;
        else
          *destination = value;
      }
  return 1;
}

int xmpk_gf16_linxfm_batch_same_left(
    const uint8_t *left, const uint8_t *right, size_t right_stride,
    uint8_t *result, size_t result_stride, unsigned dimension,
    unsigned cols, unsigned batch, int add) {
  if (!xmpk_gf16_linxfm_prepare_right_batch(
          right, right_stride, dimension, cols, batch))
    return 0;
  return xmpk_gf16_linxfm_apply_prepared(
      left, result, result_stride, add);
}

int xmpk_gf16_linxfm_matmul_add(const uint8_t *left,
                                const uint8_t *right, uint8_t *result,
                                unsigned dimension, unsigned cols, int add) {
  return xmpk_gf16_linxfm_batch_same_left(
      left, right, dimension * cols, result, dimension * cols,
      dimension, cols, 1, add);
}

int xmpk_gf127_extgemm(const uint8_t *a, const uint8_t *b, uint8_t *c,
                       unsigned m, unsigned k, unsigned n,
                       unsigned degree, int transpose_b) {
  uint64_t count;

  if (m == 0 || m > XMPK_EXT_MAX_DIM ||
      k == 0 || k > XMPK_EXT_MAX_DIM ||
      n == 0 || n > XMPK_EXT_MAX_DIM ||
      (degree != 3 && degree != 10))
    return 0;
  if (n == 1 && (uint64_t)m * k < 52u * 52u)
    return 0;
  ext_record_used(&ext_used_a, (size_t)m * k);
  ext_record_used(&ext_used_b, (size_t)k * n);
  ext_record_used(&ext_used_c, (size_t)m * n);
  for (unsigned row = 0; row < m; ++row)
    for (unsigned col = 0; col < k; ++col)
      ext_pack(&ext_a[row * k + col],
               a + (row * k + col) * degree, degree);
  for (unsigned row = 0; row < k; ++row)
    for (unsigned col = 0; col < n; ++col) {
      unsigned source =
          transpose_b ? col * k + row : row * n + col;
      ext_pack(&ext_b[row * n + col], b + source * degree, degree);
    }
  xmpk_extgemm_setup(ext_a, ext_b, ext_c);
  count = xmpk_extgemm_run(m, k, n, degree);
  if (count != (uint64_t)m * k * n * degree * degree)
    return 0;
  for (unsigned row = 0; row < m; ++row)
    for (unsigned col = 0; col < n; ++col)
      ext_unpack(c + (row * n + col) * degree,
                 &ext_c[row * n + col], degree);
  return 1;
}

int xmpk_gf127_extgemm_prepare_a(const uint8_t *a, unsigned m,
                                 unsigned k, unsigned degree) {
  if (m == 0 || m > XMPK_EXT_MAX_DIM ||
      k == 0 || k > XMPK_EXT_MAX_DIM ||
      (degree != 3 && degree != 10))
    return 0;
  ext_record_used(&ext_used_prepared, (size_t)m * k);
  for (unsigned row = 0; row < m; ++row)
    for (unsigned col = 0; col < k; ++col)
      ext_pack(&ext_prepared_a[row * k + col],
               a + (row * k + col) * degree, degree);
  ext_prepared_m = m;
  ext_prepared_k = k;
  ext_prepared_degree = degree;
  return 1;
}

int xmpk_gf127_extgemm_run_prepared_a(const uint8_t *b, uint8_t *c,
                                      unsigned n, int transpose_b) {
  uint64_t count;
  unsigned m = ext_prepared_m;
  unsigned k = ext_prepared_k;
  unsigned degree = ext_prepared_degree;

  if (m == 0 || k == 0 || n == 0 || n > XMPK_EXT_MAX_DIM)
    return 0;
  if (n == 1 && (uint64_t)m * k < 52u * 52u)
    return 0;
  ext_record_used(&ext_used_b, (size_t)k * n);
  ext_record_used(&ext_used_c, (size_t)m * n);
  for (unsigned row = 0; row < k; ++row)
    for (unsigned col = 0; col < n; ++col) {
      unsigned source = transpose_b ? col * k + row : row * n + col;
      ext_pack(&ext_b[row * n + col], b + source * degree, degree);
    }
  xmpk_extgemm_setup(ext_prepared_a, ext_b, ext_c);
  count = xmpk_extgemm_run(m, k, n, degree);
  if (count != (uint64_t)m * k * n * degree * degree)
    return 0;
  for (unsigned row = 0; row < m; ++row)
    for (unsigned col = 0; col < n; ++col)
      ext_unpack(c + (row * n + col) * degree,
                 &ext_c[row * n + col], degree);
  return 1;
}

int xmpk_gf127_extgemm_run_prepared_a_packed_rows(
    const xmpk_gf127_slot *b, xmpk_gf127_slot *c, unsigned row_offset,
    unsigned rows, unsigned n) {
  uint64_t count;
  unsigned k = ext_prepared_k;
  unsigned degree = ext_prepared_degree;

  if (ext_prepared_m == 0 || k == 0 || degree == 0 ||
      rows == 0 || row_offset + rows > ext_prepared_m ||
      n == 0 || n > XMPK_EXT_MAX_DIM)
    return 0;
  xmpk_extgemm_setup(
      ext_prepared_a + row_offset * k, b, c);
  count = xmpk_extgemm_run(rows, k, n, degree);
  return count == (uint64_t)rows * k * n * degree * degree;
}

int xmpk_gf127_extgemm_run_prepared_a_packed(
    const xmpk_gf127_slot *b, xmpk_gf127_slot *c, unsigned n) {
  return xmpk_gf127_extgemm_run_prepared_a_packed_rows(
      b, c, 0, ext_prepared_m, n);
}

void xmpk_gf127_extgemm_scrub_workspace(void) {
  static void *(*const volatile erase)(void *, int, size_t) = memset;
  erase(ext_a, 0, ext_used_a * sizeof(*ext_a));
  erase(ext_b, 0, ext_used_b * sizeof(*ext_b));
  erase(ext_c, 0, ext_used_c * sizeof(*ext_c));
  erase(ext_prepared_a, 0, ext_used_prepared * sizeof(*ext_prepared_a));
  ext_used_a = ext_used_b = ext_used_c = ext_used_prepared = 0;
  xmpk_gf127_extgemm_clear_prepared_a();
}

void xmpk_gf127_extgemm_clear_prepared_a(void) {
  ext_prepared_m = 0;
  ext_prepared_k = 0;
  ext_prepared_degree = 0;
}
