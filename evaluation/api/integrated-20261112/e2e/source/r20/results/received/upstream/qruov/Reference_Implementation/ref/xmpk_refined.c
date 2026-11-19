#include "xmpk_refined.h"
#ifdef QRUOV_XMPK_REFINED
/* One equation at a time: direct expansion, triangular outputs, fused store.
 * No experimental counters or equation batching in the complete API path. */
static xmpk_gf127_slot input_b[QRUOV_V * QRUOV_V]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot pi2_b[QRUOV_V * QRUOV_M]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot product_q[QRUOV_M * QRUOV_V]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot product_r[QRUOV_M * QRUOV_M]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot product_t[QRUOV_M]
    __attribute__((aligned(4096)));

static xmpk_gf127_slot slot_l3(const uint8_t *v)
{
    xmpk_gf127_slot s = {
        (uint64_t)v[0] | (uint64_t)v[1] << 8 | (uint64_t)v[2] << 16, 0
    };
    return s;
}

static void expand_equation(prg *rng, uint16_t eq)
{
    uint8_t sampled1[QRUOV_tau_n1], sampled2[QRUOV_tau_n2];
    size_t offset = 0;
    prepare_pi1_coeffs_from_prg(sampled1, rng, eq);
    for (unsigned i = 0; i < QRUOV_V; ++i)
        for (unsigned j = i; j < QRUOV_V; ++j, offset += 3) {
            xmpk_gf127_slot s = slot_l3(sampled1 + offset);
            input_b[i * QRUOV_V + j] = s;
            input_b[j * QRUOV_V + i] = s;
        }
    prepare_pi2_coeffs_from_prg(sampled2, rng, eq);
    for (unsigned i = 0; i < QRUOV_V * QRUOV_M; ++i)
        pi2_b[i] = slot_l3(sampled2 + i * 3);
    /* Seed expansions are public. */
}

static void store7(uint8_t *dst, size_t index, uint8_t value)
{
    size_t bit = index * 7;
    unsigned shift = bit & 7;
    uint16_t packed = (uint16_t)value << shift;
    dst[bit / 8] |= (uint8_t)packed;
    if (shift + 7 > 8) dst[bit / 8 + 1] |= (uint8_t)(packed >> 8);
}

int qruov_keygen_refined(const SEED_PK seed_pk, const SEED_SK seed_sk,
                         uint8_t pk_p3[P3_SERIALIZED_LEN])
{
    uint8_t sdt[QRUOV_M * QRUOV_V * 3];
    prg rng;
    int ok = 0;
    prepare_sdt_from_seed(sdt, seed_sk);
    prg_init(&rng, seed_pk);
    memset(pk_p3, 0, P3_SERIALIZED_LEN);
    if (!fql_prepare_MxV(sdt)) goto cleanup;
    for (unsigned eq = 0; eq < QRUOV_m; ++eq) {
        expand_equation(&rng, (uint16_t)eq);
        if (!xmpk_gf127_extgemm_run_prepared_a_packed(input_b, product_q, QRUOV_V) ||
            !xmpk_gf127_extgemm_run_prepared_a_packed(pi2_b, product_r, QRUOV_M))
            goto cleanup;
        for (unsigned i = 0; i < QRUOV_M; ++i) {
            for (unsigned k = 0; k < QRUOV_V; ++k)
                input_b[k] = product_q[i * QRUOV_V + k];
            if (!xmpk_gf127_extgemm_run_prepared_a_packed_rows(
                    input_b, product_t, i, QRUOV_M - i, 1)) goto cleanup;
            for (unsigned j = i; j < QRUOV_M; ++j) {
                size_t upper = (size_t)i * QRUOV_M - (size_t)i * (i + 1) / 2 + j;
                for (unsigned c = 0; c < 3; ++c) {
                    uint8_t t = (uint8_t)(product_t[j - i].lo >> (8 * c));
                    uint8_t a = (uint8_t)(product_r[i * QRUOV_M + j].lo >> (8 * c));
                    uint8_t b = (uint8_t)(product_r[j * QRUOV_M + i].lo >> (8 * c));
                    store7(pk_p3, (size_t)eq * P3_EQ_COEFF_LEN + upper * 3 + c,
                           gf_add(gf_sub(0, t), gf_add(a, b)));
                }
            }
        }
    }
    ok = 1;
cleanup:
    prg_free(&rng);
    secure_zero(sdt, sizeof(sdt));
    secure_zero(input_b, sizeof(input_b));
    secure_zero(product_q, sizeof(product_q));
    secure_zero(product_r, sizeof(product_r));
    secure_zero(product_t, sizeof(product_t));
    xmpk_gf127_extgemm_scrub_workspace();
    if (!ok) secure_zero(pk_p3, P3_SERIALIZED_LEN);
    return ok;
}
#endif
