#include "keygen_trial.h"
#include "fql.h"
#include "gf.h"
#include "qrop.h"
#include "util.h"

#if !defined(XMPK_ACCEL) || QRUOV_q != 127 || QRUOV_L != 3
#error "KeyGen trial currently targets q127 L3"
#endif

/* Reuse at most two equations' buffers. PRG writes directly into row-major
   interleaved B; accelerator outputs keep that layout until their consumer. */
#define TRIAL_BATCH ((128 / QRUOV_V) < 2 ? (128 / QRUOV_V) : 2)
static xmpk_gf127_slot input_b[QRUOV_V * TRIAL_BATCH * QRUOV_V]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot pi2_b[QRUOV_V * TRIAL_BATCH * QRUOV_M]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot product_q[QRUOV_M * TRIAL_BATCH * QRUOV_V]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot product_r[QRUOV_M * TRIAL_BATCH * QRUOV_M]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot product_t[QRUOV_M * TRIAL_BATCH * QRUOV_M]
    __attribute__((aligned(4096)));
static uint8_t pi3[TRIAL_BATCH][QRUOV_M * QRUOV_M * QRUOV_L];
static uint8_t upper[P3_CHUNK_COEFF_LEN];

static xmpk_gf127_slot slot_from_l3(const uint8_t *value)
{
    xmpk_gf127_slot slot = {
        (uint64_t)value[0] | (uint64_t)value[1] << 8 |
        (uint64_t)value[2] << 16, 0
    };
    return slot;
}

static void generate_b(prg *pk_prg, unsigned eq_base, unsigned count)
{
    uint8_t sampled1[QRUOV_tau_n1];
    uint8_t sampled2[QRUOV_tau_n2];
    unsigned q_columns = count * QRUOV_V;
    unsigned r_columns = count * QRUOV_M;
    for (unsigned item = 0; item < count; item++) {
        size_t offset = 0;
        prepare_pi1_coeffs_from_prg(
            sampled1, pk_prg, (uint16_t)(eq_base + item));
        for (unsigned row = 0; row < QRUOV_V; row++) {
            for (unsigned col = row; col < QRUOV_V; col++) {
                xmpk_gf127_slot value = slot_from_l3(sampled1 + offset);
                input_b[row * q_columns + item * QRUOV_V + col] = value;
                if (row != col)
                    input_b[col * q_columns + item * QRUOV_V + row] = value;
                offset += QRUOV_L;
            }
        }
        prepare_pi2_coeffs_from_prg(
            sampled2, pk_prg, (uint16_t)(eq_base + item));
        for (unsigned row = 0; row < QRUOV_V; row++) {
            for (unsigned col = 0; col < QRUOV_M; col++) {
                pi2_b[row * r_columns + item * QRUOV_M + col] =
                    slot_from_l3(sampled2 + (row * QRUOV_M + col) * QRUOV_L);
            }
        }
    }
    /* Both inputs are public-seed expansions, matching the reference policy. */
}

static size_t upper_element(unsigned i, unsigned j)
{
    return (size_t)i * QRUOV_M - (size_t)i * (i + 1) / 2 + j;
}

static uint8_t coefficient(const xmpk_gf127_slot *slot, unsigned c)
{
    return (uint8_t)(slot->lo >> (8 * c));
}

/* Output is zeroed once. Every canonical coefficient is written once, and
   disjoint bit ranges can share bytes (including equation boundaries). */
static void store_coefficient(uint8_t *serialized, size_t index, uint8_t value)
{
    size_t bit = index * QRUOV_q_LOG;
    unsigned shift = (unsigned)(bit & 7u);
    uint16_t packed = (uint16_t)value << shift;
    serialized[bit / 8] |= (uint8_t)packed;
    if (shift + QRUOV_q_LOG > 8)
        serialized[bit / 8 + 1] |= (uint8_t)(packed >> 8);
}

static void consume_product(
    uint8_t *serialized, unsigned base, unsigned count, unsigned i,
    unsigned j, const xmpk_gf127_slot *negative_product,
    unsigned item_stride, int fused)
{
    unsigned columns = count * QRUOV_M;
    for (unsigned item = 0; item < count; item++) {
        const xmpk_gf127_slot *t = negative_product + item * item_stride;
        const xmpk_gf127_slot *a =
            &product_r[i * columns + item * QRUOV_M + j];
        const xmpk_gf127_slot *b =
            &product_r[j * columns + item * QRUOV_M + i];
        for (unsigned c = 0; c < QRUOV_L; c++) {
            uint8_t negative = gf_sub(0, coefficient(t, c));
            if (fused) {
                uint8_t value = gf_add(
                    negative, gf_add(coefficient(a, c), coefficient(b, c)));
                size_t index = ((size_t)(base + item) *
                    (P3_EQ_COEFF_LEN / QRUOV_L) + upper_element(i, j)) *
                    QRUOV_L + c;
                store_coefficient(serialized, index, value);
            } else {
                pi3[item][(i * QRUOV_M + j) * QRUOV_L + c] = negative;
            }
        }
    }
}

static void finalize_unfused(unsigned base, unsigned count)
{
    unsigned columns = count * QRUOV_M;
    for (unsigned item = 0; item < count; item++) {
        for (unsigned i = 0; i < QRUOV_M; i++) {
            for (unsigned j = i; j < QRUOV_M; j++) {
                const xmpk_gf127_slot *a =
                    &product_r[i * columns + item * QRUOV_M + j];
                const xmpk_gf127_slot *b =
                    &product_r[j * columns + item * QRUOV_M + i];
                for (unsigned c = 0; c < QRUOV_L; c++) {
                    size_t index = (i * QRUOV_M + j) * QRUOV_L + c;
                    uint8_t symm = gf_add(coefficient(a, c), coefficient(b, c));
                    pi3[item][index] = gf_add(pi3[item][index], symm);
                }
            }
        }
        p3_store_upper_MxM(upper + (base + item) * P3_EQ_COEFF_LEN,
                          pi3[item]);
    }
}

int qruov_keygen_candidate_trial(
    uint8_t *serialized, const SEED_PK seed_pk, const SEED_SK seed_sk,
    unsigned eq_base, unsigned eq_count, unsigned flags,
    keygen_trial_metrics *metrics)
{
    if (!flags)
        return qruov_keygen_reference_trial(
            serialized, seed_pk, seed_sk, eq_base, eq_count, metrics);
    if (!(flags & KEYGEN_DIRECT) || flags > 15 || !eq_count ||
        eq_count > P3_CHUNK_EQS || eq_base >= QRUOV_m ||
        eq_count > QRUOV_m - eq_base)
        return 0;
    uint8_t sdt[QRUOV_M * QRUOV_V * QRUOV_L];
    prg pk_prg;
    unsigned maximum = flags & KEYGEN_BATCH ? TRIAL_BATCH : 1;
    unsigned largest = maximum < eq_count ? maximum : eq_count;
    int fused = (flags & KEYGEN_FUSED) != 0;
    int ok = 0;
    memset(metrics, 0, sizeof(*metrics));
    uint64_t instructions = keygen_trial_instret();
    uint64_t total = keygen_trial_cycle();
    uint64_t start = total;
    prepare_sdt_from_seed(sdt, seed_sk);
    int prepared = fql_prepare_MxV(sdt);
    prg_init(&pk_prg, seed_pk);
    if (fused)
        memset(serialized, 0,
               BITS2BYTE((size_t)eq_count * P3_EQ_COEFF_LEN * QRUOV_q_LOG));
    metrics->setup = keygen_trial_cycle() - start;
    if (!prepared) goto cleanup;

    for (unsigned base = 0; base < eq_count; base += maximum) {
        unsigned count = eq_count - base;
        if (count > maximum) count = maximum;
        unsigned q_columns = count * QRUOV_V;
        unsigned r_columns = count * QRUOV_M;
        start = keygen_trial_cycle();
        generate_b(&pk_prg, eq_base + base, count);
        metrics->generate += keygen_trial_cycle() - start;
        start = keygen_trial_cycle();
        if (!xmpk_gf127_extgemm_run_prepared_a_packed(
                input_b, product_q, q_columns) ||
            !xmpk_gf127_extgemm_run_prepared_a_packed(
                pi2_b, product_r, r_columns))
            goto cleanup;
        metrics->products += keygen_trial_cycle() - start;

        if (flags & KEYGEN_TRIANGLE) {
            /* For original upper entry (i,j), compute T(j,i) only.
               Each column has rows i..M-1: exactly M*(M+1)/2 outputs. */
            for (unsigned i = 0; i < QRUOV_M; i++) {
                start = keygen_trial_cycle();
                for (unsigned inner = 0; inner < QRUOV_V; inner++)
                    for (unsigned item = 0; item < count; item++)
                        input_b[inner * count + item] =
                            product_q[i * q_columns + item * QRUOV_V + inner];
                metrics->transpose += keygen_trial_cycle() - start;
                start = keygen_trial_cycle();
                if (!xmpk_gf127_extgemm_run_prepared_a_packed_rows(
                        input_b, product_t, i, QRUOV_M - i, count))
                    goto cleanup;
                metrics->quadratic += keygen_trial_cycle() - start;
                start = keygen_trial_cycle();
                for (unsigned j = i; j < QRUOV_M; j++)
                    consume_product(serialized, base, count, i, j,
                                    product_t + (j - i) * count, 1, fused);
                metrics->finalize += keygen_trial_cycle() - start;
            }
        } else {
            start = keygen_trial_cycle();
            /* One necessary transpose, directly from hardware Q output to
               final B input. No per-equation unpack/copy/repack. */
            for (unsigned inner = 0; inner < QRUOV_V; inner++)
                for (unsigned item = 0; item < count; item++)
                    for (unsigned col = 0; col < QRUOV_M; col++)
                        input_b[inner * r_columns + item * QRUOV_M + col] =
                            product_q[col * q_columns + item * QRUOV_V + inner];
            metrics->transpose += keygen_trial_cycle() - start;
            start = keygen_trial_cycle();
            if (!xmpk_gf127_extgemm_run_prepared_a_packed(
                    input_b, product_t, r_columns))
                goto cleanup;
            metrics->quadratic += keygen_trial_cycle() - start;
            start = keygen_trial_cycle();
            /* T output is [row][equation][column]; consume in place. */
            for (unsigned i = 0; i < QRUOV_M; i++) {
                for (unsigned j = i; j < QRUOV_M; j++) {
                    consume_product(
                        serialized, base, count, i, j,
                        product_t + j * r_columns + i, QRUOV_M, fused);
                }
            }
            metrics->finalize += keygen_trial_cycle() - start;
        }
        if (!fused) {
            start = keygen_trial_cycle();
            finalize_unfused(base, count);
            metrics->finalize += keygen_trial_cycle() - start;
        }
    }
    if (!fused) {
        start = keygen_trial_cycle();
        store_fq(serialized, upper, (size_t)eq_count * P3_EQ_COEFF_LEN);
        metrics->serialize = keygen_trial_cycle() - start;
    }
    ok = 1;
cleanup:
    start = keygen_trial_cycle();
    prg_free(&pk_prg);
    secure_zero(sdt, sizeof(sdt));
    /* Erase actual high-water ranges, not the unused capacity for 8 items. */
    unsigned private_columns = flags & KEYGEN_TRIANGLE ?
        largest : largest * QRUOV_M;
    secure_zero(input_b, QRUOV_V * private_columns * sizeof(*input_b));
    secure_zero(product_q, QRUOV_M * largest * QRUOV_V * sizeof(*product_q));
    secure_zero(product_r, QRUOV_M * largest * QRUOV_M * sizeof(*product_r));
    secure_zero(product_t, QRUOV_M * largest * QRUOV_M * sizeof(*product_t));
    if (!fused) {
        secure_zero(pi3, largest * sizeof(pi3[0]));
        secure_zero(upper, (size_t)eq_count * P3_EQ_COEFF_LEN);
    }
    xmpk_gf127_extgemm_scrub_workspace();
    metrics->cleanup = keygen_trial_cycle() - start;
    metrics->total = keygen_trial_cycle() - total;
    metrics->instructions = keygen_trial_instret() - instructions;
    return ok;
}
