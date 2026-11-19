#ifndef FQL_H
#define FQL_H

#include <stdint.h>
#include <string.h>
#include "qruov.h"
#include "gf.h"
#if defined(XMPK_ACCEL)
#include "xmpk_asm.h"
#endif

#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3
#define QRUOV_L3_STRIPED_CHUNKS ((QRUOV_V + 7) / 8)
typedef uint64_t qruov_l3_striped_v[QRUOV_L3_STRIPED_CHUNKS][QRUOV_L];
typedef uint64_t qruov_l3_striped_vxv
    [QRUOV_V][QRUOV_L3_STRIPED_CHUNKS][QRUOV_L];
#endif

static inline void fql_zero(uint8_t z[QRUOV_L])
{
    memset(z, 0, QRUOV_L);
}

static inline void fql_add(uint8_t z[QRUOV_L], const uint8_t x[QRUOV_L], const uint8_t y[QRUOV_L])
{
    for (int i = 0; i < QRUOV_L; i++) {
        z[i] = gf_add(x[i], y[i]);
    }
}

static inline void fql_sub(uint8_t z[QRUOV_L], const uint8_t x[QRUOV_L], const uint8_t y[QRUOV_L])
{
    for (int i = 0; i < QRUOV_L; i++) {
        z[i] = gf_sub(x[i], y[i]);
    }
}

static inline void fql_perm(uint8_t dst[QRUOV_L], const uint8_t src[QRUOV_L])
{
    for (int l = 0; l < QRUOV_L; l++) {
        dst[QRUOV_perm(l)] = src[l];
    }
}

static inline void fql_add_M(uint8_t *dst, const uint8_t *x, const uint8_t *y)
{
    for (int i = 0; i < QRUOV_M; i++) {
        fql_add(&dst[i * QRUOV_L], &x[i * QRUOV_L], &y[i * QRUOV_L]);
    }
}

static inline void fql_sub_M(uint8_t *dst, const uint8_t *x, const uint8_t *y)
{
    for (int i = 0; i < QRUOV_M; i++) {
        fql_sub(&dst[i * QRUOV_L], &x[i * QRUOV_L], &y[i * QRUOV_L]);
    }
}

static inline void fql_perm_M(uint8_t *dst, const uint8_t *src)
{
    for (int i = 0; i < QRUOV_M; i++) {
        fql_perm(&dst[i * QRUOV_L], &src[i * QRUOV_L]);
    }
}

static inline void fql_mul(uint8_t z[QRUOV_L], const uint8_t x[QRUOV_L], const uint8_t y[QRUOV_L])
{
    uint32_t acc[2 * QRUOV_L - 1];
    memset(acc, 0, sizeof(acc));
    for (int i = 0; i < QRUOV_L; i++) {
        for (int j = 0; j < QRUOV_L; j++) {
            acc[i + j] += (uint32_t)x[i] * y[j];
        }
    }
    for (int t = 2 * QRUOV_L - 2; t >= QRUOV_L; t--) {
        acc[t - QRUOV_L] += (uint32_t)QRUOV_fc0 * acc[t];
        acc[t - QRUOV_L + QRUOV_fe] += (uint32_t)QRUOV_fc * acc[t];
    }
    for (int i = 0; i < QRUOV_L; i++) {
        z[i] = gf_reduce(acc[i]);
    }
}

static inline void fql_dot(uint8_t z[QRUOV_L], const uint8_t *a, const uint8_t *b, int len)
{
    uint32_t acc[2 * QRUOV_L - 1];
    memset(acc, 0, sizeof(acc));
    for (int i = 0; i < len; i++) {
        const uint8_t *ai = &a[i * QRUOV_L];
        const uint8_t *bi = &b[i * QRUOV_L];
#if defined(XMPK_ACCEL) && QRUOV_q == 127
        int pair = 0;
        const int pairs = QRUOV_L * QRUOV_L;
        while (pair < pairs) {
            uint64_t lhs = 0;
            uint64_t rhs = 0;
            int count = pairs - pair < 8 ? pairs - pair : 8;
            for (int lane = 0; lane < count; ++lane) {
                int index = pair + lane;
                lhs |= (uint64_t)ai[index / QRUOV_L] << (8 * lane);
                rhs |= (uint64_t)bi[index % QRUOV_L] << (8 * lane);
            }
            uint64_t product = xmpk_gf127_mul8(lhs, rhs);
            for (int lane = 0; lane < count; ++lane) {
                int index = pair + lane;
                acc[index / QRUOV_L + index % QRUOV_L] +=
                    (product >> (8 * lane)) & 0xff;
            }
            pair += count;
        }
#else
        for (int j = 0; j < QRUOV_L; j++) {
            const uint32_t aij = ai[j];
            for (int k = 0; k < QRUOV_L; k++) {
                acc[j + k] += aij * bi[k];
            }
        }
#endif
    }
    for (int t = 2 * QRUOV_L - 2; t >= QRUOV_L; t--) {
        acc[t - QRUOV_L] += (uint32_t)QRUOV_fc0 * acc[t];
        acc[t - QRUOV_L + QRUOV_fe] += (uint32_t)QRUOV_fc * acc[t];
    }
    for (int i = 0; i < QRUOV_L; i++) {
        z[i] = gf_reduce(acc[i]);
    }
}

static inline void fql_dot_V(uint8_t z[QRUOV_L], const uint8_t *a, const uint8_t *b)
{
    fql_dot(z, a, b, QRUOV_V);
}

static inline void fql_dot_M(uint8_t z[QRUOV_L], const uint8_t *a, const uint8_t *b)
{
    fql_dot(z, a, b, QRUOV_M);
}

static inline void fql_mul_VxV_V(uint8_t *dst, const uint8_t *A_VxV, const uint8_t *x_V)
{
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && (QRUOV_L == 3 || QRUOV_L == 10)
    if (xmpk_gf127_extgemm(A_VxV, x_V, dst, QRUOV_V, QRUOV_V, 1, QRUOV_L, 0)) {
        return;
    }
#endif
    for (int i = 0; i < QRUOV_V; i++) {
        fql_dot_V(&dst[i * QRUOV_L], &A_VxV[i * QRUOV_V * QRUOV_L], x_V);
    }
}

#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3
static inline uint32_t fql_l3_sum_bytes(uint64_t value)
{
    uint64_t pairs = (value & UINT64_C(0x00ff00ff00ff00ff)) +
                     ((value >> 8) & UINT64_C(0x00ff00ff00ff00ff));
    uint64_t quads = (pairs & UINT64_C(0x0000ffff0000ffff)) +
                     ((pairs >> 16) & UINT64_C(0x0000ffff0000ffff));
    return (uint32_t)quads + (uint32_t)(quads >> 32);
}

static inline void fql_pack_V_l3(qruov_l3_striped_v packed,
                                 const uint8_t *x_V)
{
    memset(packed, 0, sizeof(qruov_l3_striped_v));
    for (int i = 0; i < QRUOV_V; i++) {
        const int chunk = i / 8;
        const int lane = i % 8;
        for (int coefficient = 0; coefficient < QRUOV_L; coefficient++) {
            packed[chunk][coefficient] |=
                (uint64_t)x_V[i * QRUOV_L + coefficient] << (8 * lane);
        }
    }
}

static inline void fql_mul_striped_VxV_V_l3(
    uint8_t *dst, const qruov_l3_striped_vxv matrix,
    const qruov_l3_striped_v vector)
{
    for (int row = 0; row < QRUOV_V; row++) {
        uint32_t acc[2 * QRUOV_L - 1] = {0};
        for (int chunk = 0; chunk < QRUOV_L3_STRIPED_CHUNKS; chunk++) {
            for (int i = 0; i < QRUOV_L; i++) {
                for (int j = 0; j < QRUOV_L; j++) {
                    acc[i + j] += fql_l3_sum_bytes(xmpk_gf127_mul8(
                        matrix[row][chunk][i], vector[chunk][j]));
                }
            }
        }
        for (int t = 2 * QRUOV_L - 2; t >= QRUOV_L; t--) {
            acc[t - QRUOV_L] += (uint32_t)QRUOV_fc0 * acc[t];
            acc[t - QRUOV_L + QRUOV_fe] += (uint32_t)QRUOV_fc * acc[t];
        }
        for (int i = 0; i < QRUOV_L; i++) {
            dst[row * QRUOV_L + i] = gf_reduce(acc[i]);
        }
    }
}
#endif

static inline void fql_mul_MxV_V(uint8_t *dst, const uint8_t *A_MxV, const uint8_t *x_V)
{
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && (QRUOV_L == 3 || QRUOV_L == 10)
    if (xmpk_gf127_extgemm(A_MxV, x_V, dst, QRUOV_M, QRUOV_V, 1, QRUOV_L, 0)) {
        return;
    }
#endif
    for (int i = 0; i < QRUOV_M; i++) {
        fql_dot_V(&dst[i * QRUOV_L], &A_MxV[i * QRUOV_V * QRUOV_L], x_V);
    }
}

static inline void fql_mul_MxV_VxVt(uint8_t *dst, const uint8_t *A_MxV, const uint8_t *B_VxVt)
{
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && (QRUOV_L == 3 || QRUOV_L == 10)
    if (xmpk_gf127_extgemm(A_MxV, B_VxVt, dst, QRUOV_M, QRUOV_V, QRUOV_V, QRUOV_L, 1)) {
        return;
    }
#endif
    for (int i = 0; i < QRUOV_M; i++) {
        for (int j = 0; j < QRUOV_V; j++) {
            fql_dot_V(&dst[(i * QRUOV_V + j) * QRUOV_L],
                      &A_MxV[i * QRUOV_V * QRUOV_L],
                      &B_VxVt[j * QRUOV_V * QRUOV_L]);
        }
    }
}

static inline void fql_mul_MxV_MxVt(uint8_t *dst, const uint8_t *A_MxV, const uint8_t *B_MxV)
{
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && (QRUOV_L == 3 || QRUOV_L == 10)
    if (xmpk_gf127_extgemm(A_MxV, B_MxV, dst, QRUOV_M, QRUOV_V, QRUOV_M, QRUOV_L, 1)) {
        return;
    }
#endif
    for (int i = 0; i < QRUOV_M; i++) {
        for (int j = 0; j < QRUOV_M; j++) {
            fql_dot_V(&dst[(i * QRUOV_M + j) * QRUOV_L],
                      &A_MxV[i * QRUOV_V * QRUOV_L],
                      &B_MxV[j * QRUOV_V * QRUOV_L]);
        }
    }
}

static inline void fql_mul_VxM_M(uint8_t *dst, const uint8_t *A_VxM, const uint8_t *x_M)
{
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && (QRUOV_L == 3 || QRUOV_L == 10)
    if (xmpk_gf127_extgemm(A_VxM, x_M, dst, QRUOV_V, QRUOV_M, 1, QRUOV_L, 0)) {
        return;
    }
#endif
    for (int i = 0; i < QRUOV_V; i++) {
        fql_dot_M(&dst[i * QRUOV_L], &A_VxM[i * QRUOV_M * QRUOV_L], x_M);
    }
}

static inline void fql_mul_MxM_M(uint8_t *dst, const uint8_t *A_MxM, const uint8_t *x_M)
{
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && (QRUOV_L == 3 || QRUOV_L == 10)
    if (xmpk_gf127_extgemm(A_MxM, x_M, dst, QRUOV_M, QRUOV_M, 1, QRUOV_L, 0)) {
        return;
    }
#endif
    for (int i = 0; i < QRUOV_M; i++) {
        fql_dot_M(&dst[i * QRUOV_L], &A_MxM[i * QRUOV_M * QRUOV_L], x_M);
    }
}

#if defined(XMPK_ACCEL) && QRUOV_q == 127 && (QRUOV_L == 3 || QRUOV_L == 10)
static inline int fql_prepare_MxV(const uint8_t *A_MxV)
{
    return xmpk_gf127_extgemm_prepare_a(
        A_MxV, QRUOV_M, QRUOV_V, QRUOV_L);
}

static inline void fql_clear_prepared_MxV(void)
{
    xmpk_gf127_extgemm_clear_prepared_a();
}

static inline void fql_mul_prepared_MxV_VxVt(
    uint8_t *dst, const uint8_t *A_MxV, const uint8_t *B_VxVt, int prepared)
{
    if (prepared &&
        xmpk_gf127_extgemm_run_prepared_a(B_VxVt, dst, QRUOV_V, 1)) {
        return;
    }
    fql_mul_MxV_VxVt(dst, A_MxV, B_VxVt);
}

static inline void fql_mul_prepared_MxV_MxVt(
    uint8_t *dst, const uint8_t *A_MxV, const uint8_t *B_MxV, int prepared)
{
    if (prepared &&
        xmpk_gf127_extgemm_run_prepared_a(B_MxV, dst, QRUOV_M, 1)) {
        return;
    }
    fql_mul_MxV_MxVt(dst, A_MxV, B_MxV);
}

static inline void fql_mul_prepared_MxV_V(
    uint8_t *dst, const uint8_t *A_MxV, const uint8_t *x_V, int prepared)
{
    if (prepared &&
        xmpk_gf127_extgemm_run_prepared_a(x_V, dst, 1, 0)) {
        return;
    }
    fql_mul_MxV_V(dst, A_MxV, x_V);
}
#else
static inline int fql_prepare_MxV(const uint8_t *A_MxV)
{
    (void)A_MxV;
    return 0;
}

static inline void fql_clear_prepared_MxV(void)
{
}

static inline void fql_mul_prepared_MxV_VxVt(
    uint8_t *dst, const uint8_t *A_MxV, const uint8_t *B_VxVt, int prepared)
{
    (void)prepared;
    fql_mul_MxV_VxVt(dst, A_MxV, B_VxVt);
}

static inline void fql_mul_prepared_MxV_MxVt(
    uint8_t *dst, const uint8_t *A_MxV, const uint8_t *B_MxV, int prepared)
{
    (void)prepared;
    fql_mul_MxV_MxVt(dst, A_MxV, B_MxV);
}

static inline void fql_mul_prepared_MxV_V(
    uint8_t *dst, const uint8_t *A_MxV, const uint8_t *x_V, int prepared)
{
    (void)prepared;
    fql_mul_MxV_V(dst, A_MxV, x_V);
}
#endif

#endif // FQL_H
