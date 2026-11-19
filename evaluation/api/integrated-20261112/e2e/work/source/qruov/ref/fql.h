#ifndef FQL_H
#define FQL_H

#include <stdint.h>
#include <string.h>
#include "qruov.h"
#include "gf.h"
#if defined(XMPK_ACCEL)
#include "xmpk_asm.h"
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

#endif // FQL_H
