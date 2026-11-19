#include "qruov.h"
#include "qrop.h"
#include "prg.h"
#include "util.h"
#include "linsys.h"
#include "gf.h"
#include "fql.h"
#include "qruov_common.h"

void qruov_init(void)
{
}

static void fql_neg_upper_mul_MxV_MxVt(
    uint8_t *pi3, const uint8_t *A_MxV, const uint8_t *B_MxV,
    int b_prepared)
{
    uint8_t zero[QRUOV_L];
    fql_zero(zero);
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && (QRUOV_L == 3 || QRUOV_L == 10)
    if (b_prepared) {
        uint8_t transposed[QRUOV_M * QRUOV_M * QRUOV_L];
        if (xmpk_gf127_extgemm_run_prepared_a(
                A_MxV, transposed, QRUOV_M, 1)) {
            for (int i = 0; i < QRUOV_M; i++) {
                for (int j = i; j < QRUOV_M; j++) {
                    fql_sub(
                        &pi3[(i * QRUOV_M + j) * QRUOV_L], zero,
                        &transposed[(j * QRUOV_M + i) * QRUOV_L]);
                }
            }
            secure_zero(transposed, sizeof(transposed));
            return;
        }
        secure_zero(transposed, sizeof(transposed));
    }
#else
    (void)b_prepared;
#endif
    // Only the upper triangle of pi3 is defined in ref keygen.
    for (int i = 0; i < QRUOV_M; i++) {
        for (int j = i; j < QRUOV_M; j++) {
            uint8_t tmp[QRUOV_L];
            fql_dot_V(tmp, &A_MxV[i * QRUOV_V * QRUOV_L], &B_MxV[j * QRUOV_V * QRUOV_L]);
            fql_sub(&pi3[(i * QRUOV_M + j) * QRUOV_L], zero, tmp);
        }
    }
}

static void fql_add_upper_symmetrized_MxM(uint8_t *pi3, const uint8_t *src)
{
    for (int i = 0; i < QRUOV_M; i++) {
        for (int j = i; j < QRUOV_M; j++) {
            uint8_t symm[QRUOV_L];
            fql_add(symm, &src[(i * QRUOV_M + j) * QRUOV_L], &src[(j * QRUOV_M + i) * QRUOV_L]);
            fql_add(&pi3[(i * QRUOV_M + j) * QRUOV_L], &pi3[(i * QRUOV_M + j) * QRUOV_L], symm);
        }
    }
}

#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3
#define KEYGEN_TRIANGLE_BLOCK 9
#define KEYGEN_MAX_BATCH_COLS 128
#define KEYGEN_PI1_BATCH (KEYGEN_MAX_BATCH_COLS / QRUOV_V)
#define KEYGEN_PI2_BATCH (KEYGEN_MAX_BATCH_COLS / QRUOV_M)

static xmpk_gf127_slot keygen_pi1[P3_CHUNK_EQS][QRUOV_V * QRUOV_V]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot keygen_pi2[P3_CHUNK_EQS][QRUOV_V * QRUOV_M]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot keygen_sdt_pi1[P3_CHUNK_EQS][QRUOV_M * QRUOV_V]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot keygen_sdt_pi2[P3_CHUNK_EQS][QRUOV_M * QRUOV_M]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot keygen_batch_b[QRUOV_V * KEYGEN_MAX_BATCH_COLS]
    __attribute__((aligned(4096)));
static xmpk_gf127_slot keygen_batch_c[QRUOV_M * KEYGEN_MAX_BATCH_COLS]
    __attribute__((aligned(4096)));

static xmpk_gf127_slot keygen_slot_from_l3(const uint8_t *src)
{
    xmpk_gf127_slot slot;
    slot.lo = (uint64_t)src[0] |
              (uint64_t)src[1] << 8 |
              (uint64_t)src[2] << 16;
    slot.hi = 0;
    return slot;
}

static uint8_t keygen_slot_coefficient(const xmpk_gf127_slot *slot,
                                       int coefficient)
{
    return (uint8_t)(slot->lo >> (8 * coefficient));
}

static size_t keygen_upper_offset(int row, int col)
{
    size_t element = (size_t)row * QRUOV_M -
                     (size_t)row * (row + 1) / 2 + col;
    return element * QRUOV_L;
}

static void keygen_prepare_equation_slots(prg *pk_prg, int eq, int item)
{
    uint8_t pi1_coeffs[QRUOV_tau_n1];
    uint8_t pi2_coeffs[QRUOV_tau_n2];
    size_t offset = 0;

    prepare_pi1_coeffs_from_prg(pi1_coeffs, pk_prg, (uint16_t)eq);
    for (int i = 0; i < QRUOV_V; i++) {
        for (int j = i; j < QRUOV_V; j++) {
            xmpk_gf127_slot slot =
                keygen_slot_from_l3(pi1_coeffs + offset);
            keygen_pi1[item][i * QRUOV_V + j] = slot;
            keygen_pi1[item][j * QRUOV_V + i] = slot;
            offset += QRUOV_L;
        }
    }

    prepare_pi2_coeffs_from_prg(pi2_coeffs, pk_prg, (uint16_t)eq);
    for (int i = 0; i < QRUOV_V * QRUOV_M; i++) {
        keygen_pi2[item][i] =
            keygen_slot_from_l3(pi2_coeffs + i * QRUOV_L);
    }
    secure_zero(pi2_coeffs, sizeof(pi2_coeffs));
    secure_zero(pi1_coeffs, sizeof(pi1_coeffs));
}

static int keygen_batch_initial_products(int eq_count)
{
    for (int base = 0; base < eq_count; base += KEYGEN_PI1_BATCH) {
        int count = eq_count - base;
        if (count > KEYGEN_PI1_BATCH) count = KEYGEN_PI1_BATCH;
        int columns = count * QRUOV_V;
        for (int row = 0; row < QRUOV_V; row++) {
            for (int item = 0; item < count; item++) {
                memcpy(&keygen_batch_b[row * columns + item * QRUOV_V],
                       &keygen_pi1[base + item][row * QRUOV_V],
                       QRUOV_V * sizeof(xmpk_gf127_slot));
            }
        }
        if (!xmpk_gf127_extgemm_run_prepared_a_packed(
                keygen_batch_b, keygen_batch_c, (unsigned)columns)) {
            return 0;
        }
        for (int row = 0; row < QRUOV_M; row++) {
            for (int item = 0; item < count; item++) {
                memcpy(&keygen_sdt_pi1[base + item][row * QRUOV_V],
                       &keygen_batch_c[row * columns + item * QRUOV_V],
                       QRUOV_V * sizeof(xmpk_gf127_slot));
            }
        }
    }

    for (int base = 0; base < eq_count; base += KEYGEN_PI2_BATCH) {
        int count = eq_count - base;
        if (count > KEYGEN_PI2_BATCH) count = KEYGEN_PI2_BATCH;
        int columns = count * QRUOV_M;
        for (int row = 0; row < QRUOV_V; row++) {
            for (int item = 0; item < count; item++) {
                memcpy(&keygen_batch_b[row * columns + item * QRUOV_M],
                       &keygen_pi2[base + item][row * QRUOV_M],
                       QRUOV_M * sizeof(xmpk_gf127_slot));
            }
        }
        if (!xmpk_gf127_extgemm_run_prepared_a_packed(
                keygen_batch_b, keygen_batch_c, (unsigned)columns)) {
            return 0;
        }
        for (int row = 0; row < QRUOV_M; row++) {
            for (int item = 0; item < count; item++) {
                memcpy(&keygen_sdt_pi2[base + item][row * QRUOV_M],
                       &keygen_batch_c[row * columns + item * QRUOV_M],
                       QRUOV_M * sizeof(xmpk_gf127_slot));
            }
        }
    }
    return 1;
}

static int keygen_build_upper(uint8_t *upper, int eq_count)
{
    memset(upper, 0, (size_t)eq_count * P3_EQ_COEFF_LEN);
    for (int col_base = 0; col_base < QRUOV_M;
         col_base += KEYGEN_TRIANGLE_BLOCK) {
        int cols = QRUOV_M - col_base;
        if (cols > KEYGEN_TRIANGLE_BLOCK) cols = KEYGEN_TRIANGLE_BLOCK;
        int batch_columns = eq_count * cols;
        for (int inner = 0; inner < QRUOV_V; inner++) {
            for (int item = 0; item < eq_count; item++) {
                for (int col = 0; col < cols; col++) {
                    keygen_batch_b[inner * batch_columns +
                                   item * cols + col] =
                        keygen_sdt_pi1[item]
                            [(col_base + col) * QRUOV_V + inner];
                }
            }
        }
        for (int row_base = col_base; row_base < QRUOV_M;
             row_base += KEYGEN_TRIANGLE_BLOCK) {
            int rows = QRUOV_M - row_base;
            if (rows > KEYGEN_TRIANGLE_BLOCK) {
                rows = KEYGEN_TRIANGLE_BLOCK;
            }
            if (!xmpk_gf127_extgemm_run_prepared_a_packed_rows(
                    keygen_batch_b, keygen_batch_c,
                    (unsigned)row_base, (unsigned)rows,
                    (unsigned)batch_columns)) {
                return 0;
            }
            for (int item = 0; item < eq_count; item++) {
                for (int row = 0; row < rows; row++) {
                    int matrix_row = row_base + row;
                    for (int col = 0; col < cols; col++) {
                        int matrix_col = col_base + col;
                        if (matrix_col > matrix_row) continue;
                        const xmpk_gf127_slot *slot =
                            &keygen_batch_c[
                                row * batch_columns + item * cols + col];
                        uint8_t *dst =
                            upper + item * P3_EQ_COEFF_LEN +
                            keygen_upper_offset(matrix_col, matrix_row);
                        for (int coefficient = 0;
                             coefficient < QRUOV_L; coefficient++) {
                            uint8_t value =
                                keygen_slot_coefficient(slot, coefficient);
                            dst[coefficient] =
                                value == 0 ? 0 : (uint8_t)(QRUOV_q - value);
                        }
                    }
                }
            }
        }
    }

    for (int item = 0; item < eq_count; item++) {
        for (int i = 0; i < QRUOV_M; i++) {
            for (int j = i; j < QRUOV_M; j++) {
                uint8_t *dst =
                    upper + item * P3_EQ_COEFF_LEN +
                    keygen_upper_offset(i, j);
                const xmpk_gf127_slot *forward =
                    &keygen_sdt_pi2[item][i * QRUOV_M + j];
                const xmpk_gf127_slot *reverse =
                    &keygen_sdt_pi2[item][j * QRUOV_M + i];
                for (int coefficient = 0;
                     coefficient < QRUOV_L; coefficient++) {
                    uint8_t symm = gf_add(
                        keygen_slot_coefficient(forward, coefficient),
                        keygen_slot_coefficient(reverse, coefficient));
                    dst[coefficient] = gf_add(dst[coefficient], symm);
                }
            }
        }
    }
    return 1;
}

static int keygen_optimized_chunk_prepared(uint8_t *upper, prg *pk_prg,
                                           int eq_base, int eq_count)
{
    if (eq_count <= 0 || eq_count > P3_CHUNK_EQS) return 0;
    for (int item = 0; item < eq_count; item++) {
        keygen_prepare_equation_slots(pk_prg, eq_base + item, item);
    }
    return keygen_batch_initial_products(eq_count) &&
           keygen_build_upper(upper, eq_count);
}

static void keygen_clear_sensitive_buffers(void)
{
    secure_zero(keygen_sdt_pi1, sizeof(keygen_sdt_pi1));
    secure_zero(keygen_sdt_pi2, sizeof(keygen_sdt_pi2));
    secure_zero(keygen_batch_b, sizeof(keygen_batch_b));
    secure_zero(keygen_batch_c, sizeof(keygen_batch_c));
}

int qruov_keygen_xmpk_chunk(uint8_t *upper, const SEED_PK seed_pk,
                            const SEED_SK seed_sk, int eq_base,
                            int eq_count)
{
    uint8_t sdt[QRUOV_M * QRUOV_V * QRUOV_L];
    prg pk_prg;
    int ok;
    if (eq_base < 0 || eq_base + eq_count > QRUOV_m) return 0;
    prepare_sdt_from_seed(sdt, seed_sk);
    if (!fql_prepare_MxV(sdt)) {
        secure_zero(sdt, sizeof(sdt));
        return 0;
    }
    prg_init(&pk_prg, seed_pk);
    ok = keygen_optimized_chunk_prepared(
        upper, &pk_prg, eq_base, eq_count);
    prg_free(&pk_prg);
    fql_clear_prepared_MxV();
    keygen_clear_sensitive_buffers();
    secure_zero(sdt, sizeof(sdt));
    return ok;
}

void qruov_keygen_xmpk_optimized(const SEED_PK seed_pk,
                                 const SEED_SK seed_sk,
                                 uint8_t pk_p3[P3_SERIALIZED_LEN])
{
    uint8_t sdt[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t p3_chunk[P3_CHUNK_COEFF_LEN];
    size_t p3_offset = 0;
    prg pk_prg;
    prepare_sdt_from_seed(sdt, seed_sk);
    if (!fql_prepare_MxV(sdt)) {
        memset(pk_p3, 0, P3_SERIALIZED_LEN);
        secure_zero(sdt, sizeof(sdt));
        return;
    }
    prg_init(&pk_prg, seed_pk);
    for (int eq_base = 0; eq_base < QRUOV_m; eq_base += P3_CHUNK_EQS) {
        int eq_count = QRUOV_m - eq_base;
        if (eq_count > P3_CHUNK_EQS) eq_count = P3_CHUNK_EQS;
        if (!keygen_optimized_chunk_prepared(
                p3_chunk, &pk_prg, eq_base, eq_count)) {
            memset(pk_p3, 0, P3_SERIALIZED_LEN);
            break;
        }
        size_t chunk_coeffs = (size_t)eq_count * P3_EQ_COEFF_LEN;
        store_fq(pk_p3 + p3_offset, p3_chunk, chunk_coeffs);
        p3_offset += BITS2BYTE(QRUOV_q_LOG * chunk_coeffs);
    }
    prg_free(&pk_prg);
    fql_clear_prepared_MxV();
    keygen_clear_sensitive_buffers();
    secure_zero(p3_chunk, sizeof(p3_chunk));
    secure_zero(sdt, sizeof(sdt));
}
#endif

void qruov_keygen(const SEED_PK seed_pk, const SEED_SK seed_sk, uint8_t pk_p3[P3_SERIALIZED_LEN])
{
    uint8_t sdt[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t pi1[QRUOV_V * QRUOV_V * QRUOV_L];
    uint8_t pi2t[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t sdt_pi1[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t sdt_pi2[QRUOV_M * QRUOV_M * QRUOV_L];
    uint8_t pi3[QRUOV_M * QRUOV_M * QRUOV_L];
    uint8_t p3_chunk[P3_CHUNK_COEFF_LEN];
    int chunk_eqs = 0;
    size_t p3_offset = 0;
    prepare_sdt_from_seed(sdt, seed_sk);
    int sdt_prepared = fql_prepare_MxV(sdt);
    prg pk_prg;
    prg_init(&pk_prg, seed_pk);
    for (int eq = 0; eq < QRUOV_m; eq++) {
        prepare_pi1_from_prg(pi1, &pk_prg, (uint16_t)eq);
        prepare_pi2t_from_prg(pi2t, &pk_prg, (uint16_t)eq);
        fql_mul_prepared_MxV_VxVt(sdt_pi1, sdt, pi1, sdt_prepared);
        // The lower triangle is intentionally left uninitialized: the helpers
        // below update only the upper triangle, and only that upper triangle is packed.
        fql_neg_upper_mul_MxV_MxVt(
            pi3, sdt_pi1, sdt, sdt_prepared);
        fql_mul_prepared_MxV_MxVt(sdt_pi2, sdt, pi2t, sdt_prepared);
        fql_add_upper_symmetrized_MxM(pi3, sdt_pi2);
        p3_store_upper_MxM(&p3_chunk[chunk_eqs * P3_EQ_COEFF_LEN], pi3);
        chunk_eqs++;
        if (chunk_eqs == P3_CHUNK_EQS) {
            store_fq(pk_p3 + p3_offset, p3_chunk, P3_CHUNK_COEFF_LEN);
            p3_offset += BITS2BYTE(QRUOV_q_LOG * P3_CHUNK_COEFF_LEN);
            chunk_eqs = 0;
        }
    }
    if (chunk_eqs > 0) {
        store_fq(pk_p3 + p3_offset, p3_chunk, (size_t)chunk_eqs * P3_EQ_COEFF_LEN);
    }
    prg_free(&pk_prg);
    fql_clear_prepared_MxV();
    secure_zero(sdt, sizeof(sdt));
    secure_zero(sdt_pi1, sizeof(sdt_pi1));
    secure_zero(sdt_pi2, sizeof(sdt_pi2));
}

int qruov_sign(const SEED_PK seed_pk, const SEED_SK seed_sk, const SEED_SK seed_y, const SEED_SK seed_r, const SEED_SK seed_sol, const uint8_t msg[], size_t msg_len, SALT sig_r, SIG_S sig_s)
{
    return qruov_common_sign_scalar(seed_pk, seed_sk, seed_y, seed_r, seed_sol,
                                    msg, msg_len, sig_r, sig_s);
}

#if defined(QRUOV_KEYGEN_SHORT_TEST)
#include "keygen_trial.h"
int qruov_keygen_reference_trial(
    uint8_t *serialized, const SEED_PK seed_pk, const SEED_SK seed_sk,
    unsigned eq_base, unsigned eq_count, keygen_trial_metrics *metrics)
{
    uint8_t sdt[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t pi1[QRUOV_V * QRUOV_V * QRUOV_L];
    uint8_t pi2t[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t q[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t r[QRUOV_M * QRUOV_M * QRUOV_L];
    uint8_t pi3[QRUOV_M * QRUOV_M * QRUOV_L];
    uint8_t upper[P3_CHUNK_COEFF_LEN];
    prg pk_prg;
    if (!eq_count || eq_count > P3_CHUNK_EQS ||
        eq_base >= QRUOV_m || eq_count > QRUOV_m - eq_base)
        return 0;
    memset(metrics, 0, sizeof(*metrics));
    uint64_t instructions = keygen_trial_instret();
    uint64_t total = keygen_trial_cycle();
    uint64_t start = total;
    prepare_sdt_from_seed(sdt, seed_sk);
    int prepared = fql_prepare_MxV(sdt);
    prg_init(&pk_prg, seed_pk);
    metrics->setup = keygen_trial_cycle() - start;
    for (unsigned item = 0; item < eq_count; item++) {
        start = keygen_trial_cycle();
        prepare_pi1_from_prg(pi1, &pk_prg, (uint16_t)(eq_base + item));
        prepare_pi2t_from_prg(pi2t, &pk_prg, (uint16_t)(eq_base + item));
        metrics->generate += keygen_trial_cycle() - start;
        start = keygen_trial_cycle();
        fql_mul_prepared_MxV_VxVt(q, sdt, pi1, prepared);
        metrics->products += keygen_trial_cycle() - start;
        start = keygen_trial_cycle();
        fql_neg_upper_mul_MxV_MxVt(pi3, q, sdt, prepared);
        metrics->quadratic += keygen_trial_cycle() - start;
        start = keygen_trial_cycle();
        fql_mul_prepared_MxV_MxVt(r, sdt, pi2t, prepared);
        metrics->products += keygen_trial_cycle() - start;
        start = keygen_trial_cycle();
        fql_add_upper_symmetrized_MxM(pi3, r);
        p3_store_upper_MxM(upper + item * P3_EQ_COEFF_LEN, pi3);
        metrics->finalize += keygen_trial_cycle() - start;
    }
    start = keygen_trial_cycle();
    store_fq(serialized, upper, (size_t)eq_count * P3_EQ_COEFF_LEN);
    metrics->serialize = keygen_trial_cycle() - start;
    start = keygen_trial_cycle();
    prg_free(&pk_prg);
    /* Same policy on both paths: erase all live secret-derived workspaces,
       including runtime packing buffers; public PRG matrices need no erase. */
    secure_zero(sdt, sizeof(sdt));
    secure_zero(q, sizeof(q));
    secure_zero(r, sizeof(r));
    secure_zero(pi3, sizeof(pi3));
    secure_zero(upper, (size_t)eq_count * P3_EQ_COEFF_LEN);
    xmpk_gf127_extgemm_scrub_workspace();
    metrics->cleanup = keygen_trial_cycle() - start;
    metrics->total = keygen_trial_cycle() - total;
    metrics->instructions = keygen_trial_instret() - instructions;
    return prepared;
}
#endif

int qruov_verify(const SEED_PK seed_pk, const uint8_t pk_p3[P3_SERIALIZED_LEN], const uint8_t msg[], size_t msg_len, const SALT sig_r, const SIG_S sig_s)
{
    uint8_t t[QRUOV_m];
    const int perm0 = QRUOV_perm(0);
    const uint8_t *vinegar = sig_s;
    const uint8_t *oil = sig_s + QRUOV_V * QRUOV_L;
    uint8_t p3_chunk[P3_CHUNK_COEFF_LEN];
    prg pk_prg;
    if (!qruov_common_is_canonical_s(sig_s)) return 0;
    compute_mu_and_hash(t, seed_pk, msg, msg_len, sig_r);
    prg_init(&pk_prg, seed_pk);
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3
    qruov_l3_striped_v packed_vinegar;
    fql_pack_V_l3(packed_vinegar, vinegar);
#endif
    int ok = 1;
    size_t p3_offset = 0;
    for (int eq_base = 0; eq_base < QRUOV_m; eq_base += P3_CHUNK_EQS) {
        int chunk_eqs = QRUOV_m - eq_base;
        if (chunk_eqs > P3_CHUNK_EQS) chunk_eqs = P3_CHUNK_EQS;
        size_t chunk_coeffs = (size_t)chunk_eqs * P3_EQ_COEFF_LEN;
        if (!load_fq_checked(p3_chunk, pk_p3 + p3_offset, chunk_coeffs)) {
            ok = 0;
            memset(p3_chunk, 0, chunk_coeffs);
        }
        p3_offset += BITS2BYTE(QRUOV_q_LOG * chunk_coeffs);
        for (int chunk_eq = 0; chunk_eq < chunk_eqs; chunk_eq++) {
            int eq = eq_base + chunk_eq;
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3
            qruov_l3_striped_vxv pi1;
#else
            uint8_t pi1[QRUOV_V * QRUOV_V * QRUOV_L];
#endif
            uint8_t pi2[QRUOV_tau_n2];
            uint8_t pi3[QRUOV_M * QRUOV_M * QRUOV_L];
            uint8_t pi1_v[QRUOV_V * QRUOV_L];
            uint8_t pi2_o[QRUOV_V * QRUOV_L];
            uint8_t pi3_o[QRUOV_M * QRUOV_L];
            uint8_t v_pi1_v[QRUOV_L];
            uint8_t v_pi2_o[QRUOV_L];
            uint8_t o_pi3_o[QRUOV_L];
            uint8_t lhs[QRUOV_L];
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3
            prepare_pi1_striped_from_prg(pi1, &pk_prg, (uint16_t)eq);
#else
            prepare_pi1_from_prg(pi1, &pk_prg, (uint16_t)eq);
#endif
            prepare_pi2_coeffs_from_prg(pi2, &pk_prg, (uint16_t)eq);
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3
            fql_mul_striped_VxV_V_l3(pi1_v, pi1, packed_vinegar);
#else
            fql_mul_VxV_V(pi1_v, pi1, vinegar);
#endif
            fql_dot_V(v_pi1_v, vinegar, pi1_v);
            fql_mul_VxM_M(pi2_o, pi2, oil);
            fql_dot_V(v_pi2_o, vinegar, pi2_o);
            unpack_upper_to_symm_MxM(pi3, &p3_chunk[chunk_eq * P3_EQ_COEFF_LEN]);
            fql_mul_MxM_M(pi3_o, pi3, oil);
            fql_dot_M(o_pi3_o, oil, pi3_o);
            fql_add(lhs, v_pi1_v, v_pi2_o);
            fql_add(lhs, lhs, v_pi2_o);
            fql_add(lhs, lhs, o_pi3_o);
            ok &= (lhs[perm0] == t[eq]);
        }
    }
    prg_free(&pk_prg);
#if defined(XMPK_ACCEL) && QRUOV_q == 127 && QRUOV_L == 3
    secure_zero(packed_vinegar, sizeof(packed_vinegar));
#endif
    return ok;
}
