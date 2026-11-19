#include "fql.h"
#include "prg.h"
#include "qrop.h"
#include "qruov.h"

#include <stdint.h>
#include <string.h>

extern void __attribute__((noreturn)) tohost_exit(uintptr_t code);

#define EQUATIONS P3_CHUNK_EQS

static uint8_t reference_upper[EQUATIONS * P3_EQ_COEFF_LEN]
    __attribute__((aligned(4096)));
static uint8_t optimized_upper[EQUATIONS * P3_EQ_COEFF_LEN]
    __attribute__((aligned(4096)));

static inline uint64_t read_cycle(void)
{
    uint64_t value;
    __asm__ volatile("rdcycle %0" : "=r"(value) :: "memory");
    return value;
}

static void neg_upper_product(uint8_t *pi3, const uint8_t *a,
                              const uint8_t *sdt, int prepared)
{
    uint8_t zero[QRUOV_L];
    fql_zero(zero);
    if (prepared) {
        uint8_t transposed[QRUOV_M * QRUOV_M * QRUOV_L];
        if (xmpk_gf127_extgemm_run_prepared_a(
                a, transposed, QRUOV_M, 1)) {
            for (int i = 0; i < QRUOV_M; i++) {
                for (int j = i; j < QRUOV_M; j++) {
                    fql_sub(&pi3[(i * QRUOV_M + j) * QRUOV_L], zero,
                            &transposed[(j * QRUOV_M + i) * QRUOV_L]);
                }
            }
            return;
        }
    }
    for (int i = 0; i < QRUOV_M; i++) {
        for (int j = i; j < QRUOV_M; j++) {
            uint8_t product[QRUOV_L];
            fql_dot_V(product, &a[i * QRUOV_V * QRUOV_L],
                      &sdt[j * QRUOV_V * QRUOV_L]);
            fql_sub(&pi3[(i * QRUOV_M + j) * QRUOV_L], zero, product);
        }
    }
}

static void add_upper_symmetrized(uint8_t *pi3, const uint8_t *src)
{
    for (int i = 0; i < QRUOV_M; i++) {
        for (int j = i; j < QRUOV_M; j++) {
            uint8_t symm[QRUOV_L];
            fql_add(symm, &src[(i * QRUOV_M + j) * QRUOV_L],
                    &src[(j * QRUOV_M + i) * QRUOV_L]);
            fql_add(&pi3[(i * QRUOV_M + j) * QRUOV_L],
                    &pi3[(i * QRUOV_M + j) * QRUOV_L], symm);
        }
    }
}

static int reference_chunk(uint8_t *upper, const SEED_PK seed_pk,
                           const SEED_SK seed_sk)
{
    uint8_t sdt[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t pi1[QRUOV_V * QRUOV_V * QRUOV_L];
    uint8_t pi2t[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t sdt_pi1[QRUOV_M * QRUOV_V * QRUOV_L];
    uint8_t sdt_pi2[QRUOV_M * QRUOV_M * QRUOV_L];
    uint8_t pi3[QRUOV_M * QRUOV_M * QRUOV_L];
    prg pk_prg;
    prepare_sdt_from_seed(sdt, seed_sk);
    int prepared = fql_prepare_MxV(sdt);
    prg_init(&pk_prg, seed_pk);
    for (int eq = 0; eq < EQUATIONS; eq++) {
        prepare_pi1_from_prg(pi1, &pk_prg, (uint16_t)eq);
        prepare_pi2t_from_prg(pi2t, &pk_prg, (uint16_t)eq);
        fql_mul_prepared_MxV_VxVt(
            sdt_pi1, sdt, pi1, prepared);
        neg_upper_product(pi3, sdt_pi1, sdt, prepared);
        fql_mul_prepared_MxV_MxVt(
            sdt_pi2, sdt, pi2t, prepared);
        add_upper_symmetrized(pi3, sdt_pi2);
        p3_store_upper_MxM(
            upper + eq * P3_EQ_COEFF_LEN, pi3);
    }
    prg_free(&pk_prg);
    fql_clear_prepared_MxV();
    return prepared;
}

int main(void)
{
    SEED_PK seed_pk;
    SEED_SK seed_sk;
    for (unsigned i = 0; i < sizeof(seed_pk); i++) {
        seed_pk[i] = (uint8_t)(17u * i + 3u);
    }
    for (unsigned i = 0; i < sizeof(seed_sk); i++) {
        seed_sk[i] = (uint8_t)(29u * i + 5u);
    }

#if PERF_MODE == 2
    if (!reference_chunk(reference_upper, seed_pk, seed_sk) ||
        !qruov_keygen_xmpk_chunk(
            optimized_upper, seed_pk, seed_sk, 0, EQUATIONS) ||
        memcmp(reference_upper, optimized_upper,
               sizeof(reference_upper)) != 0) {
        tohost_exit(0x3fffffff);
    }
    tohost_exit(0);
#else
    uint64_t start = read_cycle();
#if PERF_MODE == 1
    if (!qruov_keygen_xmpk_chunk(
            optimized_upper, seed_pk, seed_sk, 0, EQUATIONS)) {
        tohost_exit(0x3ffffffe);
    }
#else
    if (!reference_chunk(reference_upper, seed_pk, seed_sk)) {
        tohost_exit(0x3ffffffe);
    }
#endif
    uint64_t cycles = read_cycle() - start;
    tohost_exit((cycles & UINT64_C(0x3fffffff)) + 1);
#endif
}
