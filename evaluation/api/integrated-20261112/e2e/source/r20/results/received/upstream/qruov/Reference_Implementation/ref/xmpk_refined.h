#ifndef QRUOV_XMPK_REFINED_H
#define QRUOV_XMPK_REFINED_H
#include "fql.h"
#include "gf.h"
#include "qrop.h"
#include "util.h"

#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3 && \
    QRUOV_fc == 1 && QRUOV_fc0 == 1 && QRUOV_fe == 1 && \
    QRUOV_V <= 128 && QRUOV_M <= 128
#define QRUOV_XMPK_REFINED 1
typedef qruov_l3_striped_v qruov_refined_mxv[QRUOV_M];
_Static_assert((uint64_t)QRUOV_V * QRUOV_M * 3 * 126 * 126 <= UINT32_MAX,
               "coefficient-zero cross contraction must fit uint32");
_Static_assert((uint64_t)QRUOV_M * (QRUOV_M + 1) / 2 * 3 * 126 * 126 <= UINT32_MAX,
               "coefficient-zero upper contraction must fit uint32");

static inline uint8_t qruov_refined_dot0(
    const uint8_t *a, const uint8_t *b, unsigned n)
{
    uint32_t sum = 0;
    for (unsigned i = 0; i < n; ++i, a += 3, b += 3)
        sum += (uint32_t)a[0] * b[0] + (uint32_t)a[1] * b[2] +
               (uint32_t)a[2] * b[1];
    return gf_reduce(sum);
}

static inline void qruov_refined_pack_mxv(
    qruov_refined_mxv dst, const uint8_t *src)
{
    for (unsigned row = 0; row < QRUOV_M; ++row)
        fql_pack_V_l3(dst[row], src + row * QRUOV_V * 3);
}

static inline void qruov_refined_pi2t(
    qruov_refined_mxv dst, prg *rng, uint16_t eq)
{
    uint8_t sampled[QRUOV_tau_n2];
    prepare_pi2_coeffs_from_prg(sampled, rng, eq);
    memset(dst, 0, sizeof(qruov_refined_mxv));
    for (unsigned i = 0; i < QRUOV_V; ++i)
        for (unsigned j = 0; j < QRUOV_M; ++j)
            for (unsigned c = 0; c < 3; ++c)
                dst[j][i / 8][c] |=
                    (uint64_t)sampled[(i * QRUOV_M + j) * 3 + c] << (8 * (i % 8));
    secure_zero(sampled, sizeof(sampled));
}

static inline void qruov_refined_mxv_mul(
    uint8_t *dst, const qruov_refined_mxv a, const qruov_l3_striped_v b)
{
    for (unsigned row = 0; row < QRUOV_M; ++row) {
        uint32_t sum[5] = {0};
        for (unsigned k = 0; k < QRUOV_L3_STRIPED_CHUNKS; ++k)
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned j = 0; j < 3; ++j)
                    sum[i + j] += fql_l3_sum_bytes(
                        xmpk_gf127_mul8(a[row][k][i], b[k][j]));
        sum[1] += sum[4]; sum[2] += sum[4];
        sum[0] += sum[3]; sum[1] += sum[3];
        for (unsigned c = 0; c < 3; ++c) dst[row * 3 + c] = gf_reduce(sum[c]);
    }
}

static inline void qruov_refined_fixed_products(
    uint8_t *cross, uint8_t *upper, const uint8_t *v, const uint8_t *oil)
{
    for (unsigned i = 0; i < QRUOV_V; ++i)
        for (unsigned j = 0; j < QRUOV_M; ++j)
            fql_mul(cross + (i * QRUOV_M + j) * 3, v + i * 3, oil + j * 3);
    unsigned offset = 0;
    for (unsigned i = 0; i < QRUOV_M; ++i)
        for (unsigned j = i; j < QRUOV_M; ++j, offset += 3) {
            fql_mul(upper + offset, oil + i * 3, oil + j * 3);
            if (i != j) fql_add(upper + offset, upper + offset, upper + offset);
        }
}
int qruov_keygen_refined(const SEED_PK seed_pk, const SEED_SK seed_sk,
                         uint8_t pk_p3[P3_SERIALIZED_LEN]);
#endif
#endif
